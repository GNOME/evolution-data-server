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
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

// fixme, use own type funcs
#include <ctype.h>

#ifdef CAMEL_HAVE_NSS
#include <nspr.h>
#include <prio.h>
#include <prerror.h>
#include <prerr.h>
#endif

#include "camel-imapx-utils.h"
#include "camel-imapx-stream.h"
#include "camel-imapx-server.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-store.h"
#include "camel-imapx-summary.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define c(x) camel_imapx_debug(command, x)
#define e(x) camel_imapx_debug(extra, x)

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

/* How many message headers to fetch at a time update summary for new messages*/
#define BATCH_FETCH_COUNT 500

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

void imapx_uidset_init(struct _uidset_state *ss, gint total, gint limit);
gint imapx_uidset_done(struct _uidset_state *ss, struct _CamelIMAPXCommand *ic);
gint imapx_uidset_add(struct _uidset_state *ss, struct _CamelIMAPXCommand *ic, const gchar *uid);
static gboolean imapx_command_idle_stop (CamelIMAPXServer *is, GError **error);
static gint imapx_continuation(CamelIMAPXServer *imap, gboolean litplus, GError **error);
static gboolean imapx_disconnect (CamelIMAPXServer *is);
static gint imapx_uid_cmp(gconstpointer ap, gconstpointer bp, gpointer data);

typedef struct _CamelIMAPXCommandPart CamelIMAPXCommandPart;

typedef enum {
	CAMEL_IMAPX_COMMAND_SIMPLE = 0,
	CAMEL_IMAPX_COMMAND_DATAWRAPPER,
	CAMEL_IMAPX_COMMAND_STREAM,
	CAMEL_IMAPX_COMMAND_AUTH,
	CAMEL_IMAPX_COMMAND_FILE,
	CAMEL_IMAPX_COMMAND_STRING,
	CAMEL_IMAPX_COMMAND_MASK = 0xff,
	CAMEL_IMAPX_COMMAND_LITERAL_PLUS = 0x4000, /* continuation with literal+ */
	CAMEL_IMAPX_COMMAND_CONTINUATION = 0x8000 /* does this command expect continuation? */
} camel_imapx_command_part_t;

struct _CamelIMAPXCommandPart {
	struct _CamelIMAPXCommandPart *next;
	struct _CamelIMAPXCommandPart *prev;

	struct _CamelIMAPXCommand *parent;

	gint data_size;
	gchar *data;

	camel_imapx_command_part_t type;

	gint ob_size;
	gpointer ob;
};

typedef gint (*CamelIMAPXEngineFunc)(struct _CamelIMAPXServer *engine, guint32 id, gpointer data);
typedef void (*CamelIMAPXCommandFunc)(struct _CamelIMAPXServer *engine, struct _CamelIMAPXCommand *);

struct _CamelIMAPXCommand {
	struct _CamelIMAPXCommand *next, *prev;

	CamelIMAPXServer *is;
	gint pri;

	const gchar *name;	/* command name/type (e.g. FETCH) */

	CamelFolder *select;		/* folder to select */

	struct _status_info *status; /* status for command, indicates it is complete if != NULL */

	/* If exception is set, it means we were not able to parse above status, it might be
	   because user cancelled the operation or io error */
	GError *error;

	guint32 tag;

	struct _CamelStreamMem *mem;	/* for building the part TOOD: just use a GString? */
	CamelDList parts;
	CamelIMAPXCommandPart *current;

	/* used for running some commands syncronously */
	EFlag *flag;

	/* responsible for free'ing the command */
	CamelIMAPXCommandFunc complete;
	struct _CamelIMAPXJob *job;
};

CamelIMAPXCommand *camel_imapx_command_new(CamelIMAPXServer *is, const gchar *name, CamelFolder *select, const gchar *fmt, ...);
void camel_imapx_command_add(CamelIMAPXCommand *ic, const gchar *fmt, ...);
void camel_imapx_command_free(CamelIMAPXCommand *ic);
void camel_imapx_command_close(CamelIMAPXCommand *ic);
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
	IMAPX_JOB_GET_MESSAGE = 1<<0,
	IMAPX_JOB_APPEND_MESSAGE = 1<<1,
	IMAPX_JOB_COPY_MESSAGE = 1<<2,
	IMAPX_JOB_FETCH_NEW_MESSAGES = 1<<3,
	IMAPX_JOB_REFRESH_INFO = 1<<4,
	IMAPX_JOB_SYNC_CHANGES = 1<<5,
	IMAPX_JOB_EXPUNGE = 1<<6,
	IMAPX_JOB_NOOP = 1<<7,
	IMAPX_JOB_IDLE = 1<<8,
	IMAPX_JOB_LIST = 1<<9,
	IMAPX_JOB_MANAGE_SUBSCRIPTION = 1<<10,
	IMAPX_JOB_CREATE_FOLDER = 1<<11,
	IMAPX_JOB_DELETE_FOLDER = 1<<12,
	IMAPX_JOB_RENAME_FOLDER = 1<<13,
};

/* Operations on the store (folder_tree) will have highest priority as we know for sure they are sync
   and user triggered. */
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
	CamelMsg msg;

	GError *error;

	void (*start)(CamelIMAPXServer *is, struct _CamelIMAPXJob *job);

	// ??
	//CamelOperation *op;

	gint noreply:1;		/* dont wait for reply */
	guint32 type;		/* operation type */
	gint pri;		/* the command priority */
	gshort commands;		/* counts how many commands are outstanding */

	CamelFolder *folder;
	CamelOperation *op;

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
static void imapx_command_copy_messages_step_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic);
static gint imapx_refresh_info_uid_cmp(gconstpointer ap, gconstpointer bp);
static gint imapx_uids_array_cmp (gconstpointer ap, gconstpointer bp);
static gboolean imapx_server_sync_changes(CamelIMAPXServer *is, CamelFolder *folder, gint pri, GError **error);

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
	EFlag *idle_start_watch;
	GThread *idle_thread;

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
static gboolean camel_imapx_server_idle (CamelIMAPXServer *is, CamelFolder *folder, GError **error);

enum {
	USE_SSL_NEVER,
	USE_SSL_ALWAYS,
	USE_SSL_WHEN_POSSIBLE
};

#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)

static gboolean imapx_select(CamelIMAPXServer *is, CamelFolder *folder, gboolean force, GError **error);

G_DEFINE_TYPE (CamelIMAPXServer, camel_imapx_server, CAMEL_TYPE_OBJECT)

/*
  this creates a uid (or sequence number) set directly into a command,
  if total is set, then we break it up into total uids. (i.e. command time)
  if limit is set, then we break it up into limit entries (i.e. command length)
*/
void
imapx_uidset_init(struct _uidset_state *ss, gint total, gint limit)
{
	ss->uids = 0;
	ss->entries = 0;
	ss->start = 0;
	ss->last = 0;
	ss->total = total;
	ss->limit = limit;
}

gint
imapx_uidset_done(struct _uidset_state *ss, CamelIMAPXCommand *ic)
{
	gint ret = 0;

	if (ss->last != 0 && ss->last != ss->start) {
		camel_imapx_command_add(ic, ":%d", ss->last);
	}

	ret = ss->last != 0;

	ss->start = 0;
	ss->last = 0;
	ss->uids = 0;
	ss->entries = 0;

	return ret;
}

gint
imapx_uidset_add(struct _uidset_state *ss, CamelIMAPXCommand *ic, const gchar *uid)
{
	guint32 uidn;

	uidn = strtoul(uid, NULL, 10);
	if (uidn == 0)
		return -1;

	ss->uids++;

	e(printf("uidset add '%s'\n", uid));

	if (ss->last == 0) {
		e(printf(" start\n"));
		camel_imapx_command_add(ic, "%d", uidn);
		ss->entries++;
		ss->start = uidn;
	} else {
		if (ss->last != uidn-1) {
			if (ss->last == ss->start) {
				e(printf(" ,next\n"));
				camel_imapx_command_add(ic, ",%d", uidn);
				ss->entries++;
			} else {
				e(printf(" :range\n"));
				camel_imapx_command_add(ic, ":%d,%d", ss->last, uidn);
				ss->entries+=2;
			}
			ss->start = uidn;
		}
	}

	ss->last = uidn;

	if ((ss->limit && ss->entries >= ss->limit)
	    || (ss->total && ss->uids >= ss->total)) {
		e(printf(" done, %d entries, %d uids\n", ss->entries, ss->uids));
		imapx_uidset_done(ss, ic);
		return 1;
	}

	return 0;
}

static void
imapx_command_add_part(CamelIMAPXCommand *ic, camel_imapx_command_part_t type, gpointer o)
{
	CamelIMAPXCommandPart *cp;
	CamelStreamNull *null;
	GByteArray *byte_array;
	guint ob_size = 0;

	/* TODO: literal+? */

	switch (type & CAMEL_IMAPX_COMMAND_MASK) {
	case CAMEL_IMAPX_COMMAND_DATAWRAPPER:
	case CAMEL_IMAPX_COMMAND_STREAM: {
		CamelObject *ob = o;

		/* TODO: seekable streams we could just seek to the end and back */
		null = (CamelStreamNull *)camel_stream_null_new();
		if ( (type & CAMEL_IMAPX_COMMAND_MASK) == CAMEL_IMAPX_COMMAND_DATAWRAPPER) {
			camel_data_wrapper_write_to_stream((CamelDataWrapper *)ob, (CamelStream *)null, NULL);
		} else {
			camel_stream_reset((CamelStream *)ob, NULL);
			camel_stream_write_to_stream((CamelStream *)ob, (CamelStream *)null, NULL);
			camel_stream_reset((CamelStream *)ob, NULL);
		}
		type |= CAMEL_IMAPX_COMMAND_LITERAL_PLUS;
		g_object_ref (ob);
		ob_size = null->written;
		g_object_unref (null);
		break;
	}
	case CAMEL_IMAPX_COMMAND_AUTH: {
		CamelObject *ob = o;

		/* we presume we'll need to get additional data only if we're not authenticated yet */
		g_object_ref (ob);
		camel_stream_printf((CamelStream *)ic->mem, "%s", camel_sasl_get_mechanism (CAMEL_SASL (ob)));
		if (!camel_sasl_get_authenticated((CamelSasl *)ob))
			type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		break;
	}
	case CAMEL_IMAPX_COMMAND_FILE: {
		gchar *path = o;
		struct stat st;

		if (g_stat(path, &st) == 0) {
			o = g_strdup(o);
			ob_size = st.st_size;
		} else
			o = NULL;

		type |= CAMEL_IMAPX_COMMAND_LITERAL_PLUS;
		break;
	}
	case CAMEL_IMAPX_COMMAND_STRING:
		o = g_strdup(o);
		ob_size = strlen(o);
		type |= CAMEL_IMAPX_COMMAND_LITERAL_PLUS;
		break;
	default:
		ob_size = 0;
	}

	if (type & CAMEL_IMAPX_COMMAND_LITERAL_PLUS) {
		if (ic->is->cinfo && ic->is->cinfo->capa & IMAPX_CAPABILITY_LITERALPLUS) {
			camel_stream_printf((CamelStream *)ic->mem, "{%u+}", ob_size);
		} else {
			type &= ~CAMEL_IMAPX_COMMAND_LITERAL_PLUS;
			type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
			camel_stream_printf((CamelStream *)ic->mem, "{%u}", ob_size);
		}
	}

	byte_array = camel_stream_mem_get_byte_array (ic->mem);

	cp = g_malloc0(sizeof(*cp));
	cp->type = type;
	cp->ob_size = ob_size;
	cp->ob = o;
	cp->data_size = byte_array->len;
	cp->data = g_malloc(cp->data_size+1);
	memcpy(cp->data, byte_array->data, cp->data_size);
	cp->data[cp->data_size] = 0;

	camel_stream_reset((CamelStream *)ic->mem, NULL);
	/* FIXME: hackish? */
	g_byte_array_set_size(byte_array, 0);

	camel_dlist_addtail(&ic->parts, (CamelDListNode *)cp);
}

static void
imapx_command_addv(CamelIMAPXCommand *ic, const gchar *fmt, va_list ap)
{
	const gchar *p, *ps, *start;
	guchar c;
	guint width;
	gchar ch;
	gint llong;
	gint left;
	gint fill;
	gint zero;
	gchar *s;
	gchar *P;
	gint d;
	glong l;
	guint32 f;
	CamelFlag *F;
	CamelStream *S;
	CamelDataWrapper *D;
	CamelSasl *A;
	gchar buffer[16];
	CamelFolder *folder;
	CamelStore *parent_store;
	gchar *fname = NULL, *encoded = NULL;
	const gchar *full_name;

	c(printf("adding command, fmt = '%s'\n", fmt));

	p = fmt;
	ps = fmt;
	while (( c = *p++ )) {
		switch (c) {
		case '%':
			if (*p == '%') {
				camel_stream_write((CamelStream *)ic->mem, ps, p-ps, NULL);
				p++;
				ps = p;
			} else {
				camel_stream_write((CamelStream *)ic->mem, ps, p-ps-1, NULL);
				start = p-1;
				width = 0;
				left = FALSE;
				fill = FALSE;
				zero = FALSE;
				llong = 0;

				do {
					c = *p++;
					if (c == '0')
						zero = TRUE;
					else if ( c== '-')
						left = TRUE;
					else
						break;
				} while (c);

				do {
					// FIXME: ascii isdigit
					if (isdigit(c))
						width = width * 10 + (c-'0');
					else
						break;
				} while ((c = *p++));

				while (c == 'l') {
					llong++;
					c = *p++;
				}

				switch (c) {
				case 'A': /* auth object - sasl auth, treat as special kind of continuation */
					A = va_arg(ap, CamelSasl *);
					imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_AUTH, A);
					break;
				case 'S': /* stream */
					S = va_arg(ap, CamelStream *);
					c(printf("got stream '%p'\n", S));
					imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_STREAM, S);
					break;
				case 'D': /* datawrapper */
					D = va_arg(ap, CamelDataWrapper *);
					c(printf("got data wrapper '%p'\n", D));
					imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_DATAWRAPPER, D);
					break;
				case 'P': /* filename path */
					P = va_arg(ap, gchar *);
					c(printf("got file path '%s'\n", P));
					imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_FILE, P);
					break;
				case 't': /* token */
					s = va_arg(ap, gchar *);
					camel_stream_write((CamelStream *)ic->mem, s, strlen(s), NULL);
					break;
				case 's': /* simple string */
					s = va_arg(ap, gchar *);
					c(printf("got string '%s'\n", s));
				output_string:
					if (*s) {
						guchar mask = imapx_is_mask(s);

						if (mask & IMAPX_TYPE_ATOM_CHAR)
							camel_stream_write((CamelStream *)ic->mem, s, strlen(s), NULL);
						else if (mask & IMAPX_TYPE_TEXT_CHAR) {
							camel_stream_write((CamelStream *)ic->mem, "\"", 1, NULL);
							while (*s) {
								gchar *start = s;

								while (*s && imapx_is_quoted_char(*s))
									s++;
								camel_stream_write((CamelStream *)ic->mem, start, s-start, NULL);
								if (*s) {
									camel_stream_write((CamelStream *)ic->mem, "\\", 1, NULL);
									camel_stream_write((CamelStream *)ic->mem, s, 1, NULL);
									s++;
								}
							}
							camel_stream_write((CamelStream *)ic->mem, "\"", 1, NULL);
						} else {
							imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_STRING, s);
						}
					} else {
						camel_stream_write((CamelStream *)ic->mem, "\"\"", 2, NULL);
					}
					if (encoded) {
						g_free(encoded);
						encoded = NULL;
					}
					break;
				case 'f': /* imap folder name */
					folder = va_arg(ap, CamelFolder *);
					full_name = camel_folder_get_full_name (folder);
					c(printf("got folder '%s'\n", full_name));
					parent_store = camel_folder_get_parent_store (folder);
					fname = camel_imapx_store_summary_full_from_path(((CamelIMAPXStore *) parent_store)->summary, full_name);
					if (fname) {
						encoded = camel_utf8_utf7(fname);
						g_free (fname);
					} else
						encoded = camel_utf8_utf7 (full_name);

					if (encoded) {
						s = encoded;
						goto output_string;
					} else
						camel_stream_write((CamelStream *)ic->mem, "\"\"", 2, NULL);

					break;
				case 'F': /* IMAP flags set */
					f = va_arg(ap, guint32);
					F = va_arg(ap, CamelFlag *);
					imapx_write_flags((CamelStream *)ic->mem, f, F, NULL);
					break;
				case 'c':
					d = va_arg(ap, gint);
					ch = d;
					camel_stream_write((CamelStream *)ic->mem, &ch, 1, NULL);
					break;
				case 'd': /* int/unsigned */
				case 'u':
					if (llong == 1) {
						l = va_arg(ap, glong);
						c(printf("got glong '%d'\n", (gint)l));
						memcpy(buffer, start, p-start);
						buffer[p-start] = 0;
						camel_stream_printf((CamelStream *)ic->mem, buffer, l);
					} else if (llong == 2) {
						guint64 i64 = va_arg (ap, guint64);
						c(printf("got guint64 '%d'\n", (gint)i64));
						memcpy (buffer, start, p-start);
						buffer[p-start] = 0;
						camel_stream_printf ((CamelStream *)ic->mem, buffer, i64);
					} else {
						d = va_arg(ap, gint);
						c(printf("got gint '%d'\n", d));
						memcpy(buffer, start, p-start);
						buffer[p-start] = 0;
						camel_stream_printf((CamelStream *)ic->mem, buffer, d);
					}
					break;
				}

				ps = p;
			}
			break;
		case '\\':	/* only for \\ really, we dont support \n\r etc at all */
			c = *p;
			if (c) {
				g_assert(c == '\\');
				camel_stream_write((CamelStream *)ic->mem, ps, p-ps, NULL);
				p++;
				ps = p;
			}
		}
	}

	camel_stream_write((CamelStream *)ic->mem, ps, p-ps-1, NULL);
}

CamelIMAPXCommand *
camel_imapx_command_new(CamelIMAPXServer *is, const gchar *name, CamelFolder *select, const gchar *fmt, ...)
{
	CamelIMAPXCommand *ic;
	static gint tag = 0;
	va_list ap;

	ic = g_malloc0(sizeof(*ic));
	ic->tag = tag++;
	ic->name = name;
	ic->mem = (CamelStreamMem *)camel_stream_mem_new();
	ic->select = select;
	ic->is = is;
	camel_dlist_init(&ic->parts);

	if (fmt && fmt[0]) {
		va_start(ap, fmt);
		imapx_command_addv(ic, fmt, ap);
		va_end(ap);
	}

	return ic;
}

void
camel_imapx_command_add(CamelIMAPXCommand *ic, const gchar *fmt, ...)
{
	va_list ap;

	g_assert(ic->mem);	/* gets reset on queue */

	if (fmt && fmt[0]) {
		va_start(ap, fmt);
		imapx_command_addv(ic, fmt, ap);
		va_end(ap);
	}
}

void
camel_imapx_command_free(CamelIMAPXCommand *ic)
{
	CamelIMAPXCommandPart *cp;

	if (ic == NULL)
		return;

	if (ic->mem)
		g_object_unref (ic->mem);
	imapx_free_status(ic->status);

	while ((cp = ((CamelIMAPXCommandPart *)camel_dlist_remhead(&ic->parts)))) {
		g_free(cp->data);
		if (cp->ob) {
			switch (cp->type & CAMEL_IMAPX_COMMAND_MASK) {
			case CAMEL_IMAPX_COMMAND_FILE:
			case CAMEL_IMAPX_COMMAND_STRING:
				g_free(cp->ob);
				break;
			default:
				g_object_unref (cp->ob);
			}
		}
		g_free(cp);
	}

	/* Do NOT try to free the GError.  If set it should have been
	 * propagated to the CamelIMAPXJob, so it's either NULL or the
	 * CamelIMAPXJob owns it now. */

	g_free(ic);
}

void
camel_imapx_command_close(CamelIMAPXCommand *ic)
{
	if (ic->mem) {
		GByteArray *byte_array;

		byte_array = camel_stream_mem_get_byte_array (ic->mem);

		c(printf("completing command buffer is [%d] '%.*s'\n", byte_array->len, (gint)byte_array->len, byte_array->data));
		if (byte_array->len > 0)
			imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_SIMPLE, NULL);

		g_object_unref (ic->mem);
		ic->mem = NULL;
	}
}

/* Must hold QUEUE_LOCK */
static gboolean
imapx_command_start (CamelIMAPXServer *imap, CamelIMAPXCommand *ic)
{
	CamelIMAPXCommandPart *cp;

	camel_imapx_command_close(ic);
	cp = (CamelIMAPXCommandPart *)ic->parts.head;
	g_assert(cp->next);
	ic->current = cp;

	/* TODO: If we support literal+ we should be able to write the whole command out
	   at this point .... >here< */

	if (cp->type & (CAMEL_IMAPX_COMMAND_CONTINUATION|CAMEL_IMAPX_COMMAND_LITERAL_PLUS))
		imap->literal = ic;

	camel_dlist_addtail(&imap->active, (CamelDListNode *)ic);

	g_static_rec_mutex_lock (&imap->ostream_lock);

	c(printf("Starting command (active=%d,%s) %c%05u %s\r\n", camel_dlist_length(&imap->active), imap->literal?" literal":"", imap->tagprefix, ic->tag, cp->data));
	if (!imap->stream || camel_stream_printf((CamelStream *)imap->stream, "%c%05u %s\r\n", imap->tagprefix, ic->tag, cp->data) == -1) {
		g_set_error (
			&ic->error, CAMEL_IMAPX_ERROR, 1,
			"Failed to issue the command");
	err:
		g_static_rec_mutex_unlock (&imap->ostream_lock);

		camel_dlist_remove ((CamelDListNode *)ic);
		if (ic && ic->complete)
			ic->complete (imap, ic);
		return FALSE;
	}
	while (imap->literal == ic &&
	       ic->current->type & CAMEL_IMAPX_COMMAND_LITERAL_PLUS) {
		/* Sent LITERAL+ continuation immediately */
		if (!imapx_continuation(imap, TRUE, &ic->error))
			goto err;
	}

	g_static_rec_mutex_unlock (&imap->ostream_lock);

	return TRUE;
}

static gboolean duplicate_fetch_or_refresh(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (!ic->job)
		return FALSE;

	if (!(ic->job->type & (IMAPX_JOB_FETCH_NEW_MESSAGES|IMAPX_JOB_REFRESH_INFO)))
		return FALSE;

	if (imapx_match_active_job (is, IMAPX_JOB_FETCH_NEW_MESSAGES|IMAPX_JOB_REFRESH_INFO, NULL)) {
		c(printf("Not yet sending duplicate fetch/refresh %s command\n", ic->name));
		return TRUE;
	}

	return FALSE;
}
/* See if we can start another task yet.

	If we're waiting for a literal, we cannot proceed.

	If we're about to change the folder we're
	looking at from user-direction, we dont proceed.

	If we have a folder selected, first see if any
	jobs are waiting on it, but only if they are
	at least as high priority as anything we
	have running.

	If we dont, select the first folder required,
	then queue all the outstanding jobs on it, that
	are at least as high priority as the first.

	must have QUEUE lock */

static void
imapx_command_start_next(CamelIMAPXServer *is, GError **error)
{
	CamelIMAPXCommand *ic, *nc;
	gint count = 0;
	gint pri = -128;

	c(printf("** Starting next command\n"));
	if (is->literal) {
		c(printf("* no; waiting for literal '%s'\n", is->literal->name));
		return;
	}

	if (is->select_pending) {
		c(printf("-- Checking job queue for non-folder jobs\n"));
		ic = (CamelIMAPXCommand *)is->queue.head;
		nc = ic->next;
		while (nc && is->literal == NULL && count < MAX_COMMANDS && ic->pri >= pri) {
			c(printf("-- %3d '%s'?\n", (gint)ic->pri, ic->name));
			if (!ic->select) {
				c(printf("--> starting '%s'\n", ic->name));
				pri = ic->pri;
				camel_dlist_remove((CamelDListNode *)ic);
				imapx_command_start(is, ic);
				count++;
			}
			ic = nc;
			nc = nc->next;
		}

		if (count)
			return;

		c(printf("* no, waiting for pending select '%s'\n", camel_folder_get_full_name (is->select_pending)));
		return;
	}

	if (imapx_idle_supported (is) && is->state == IMAPX_SELECTED) {
		gboolean empty = imapx_is_command_queue_empty (is);

		if (imapx_in_idle (is) && !camel_dlist_empty (&is->queue)) {
			/* if imapx_stop_idle() returns FALSE, it was only
			   pending and we can go ahead and send a new command
			   immediately. If it returns TRUE, either it sent the
			   DONE to exit IDLE mode, or there was an error.
			   Either way, we do nothing more right now. */
			if (imapx_stop_idle (is, error)) {
				c(printf ("waiting for idle to stop \n"));
				return;
			}
		} else if (empty && !imapx_in_idle (is)) {
			imapx_start_idle (is);
			c(printf ("starting idle \n"));
			return;
		}
	}

	ic = (CamelIMAPXCommand *)is->queue.head;
	nc = ic->next;
	if (nc == NULL) {
		c(printf("* no, no jobs\n"));
		return;
	}

	/* See if any queued jobs on this select first */
	if (is->select_folder) {
		c(printf("- we're selected on '%s', current jobs?\n",
			 camel_folder_get_full_name(is->select_folder)));
		for (ic = (CamelIMAPXCommand *)is->active.head;ic->next;ic=ic->next) {
			c(printf("-  %3d '%s'\n", (gint)ic->pri, ic->name));
			if (ic->pri > pri)
				pri = ic->pri;
			count++;
			if (count > MAX_COMMANDS) {
				c(printf("** too many jobs busy, waiting for results for now\n"));
				return;
			}
		}

		c(printf("-- Checking job queue\n"));
		count = 0;
		ic = (CamelIMAPXCommand *)is->queue.head;
		nc = ic->next;
		while (nc && is->literal == NULL && count < MAX_COMMANDS && ic->pri >= pri) {
			c(printf("-- %3d '%s'?\n", (gint)ic->pri, ic->name));
			if (!ic->select || ((ic->select == is->select_folder) &&
					    !duplicate_fetch_or_refresh(is, ic))) {
				c(printf("--> starting '%s'\n", ic->name));
				pri = ic->pri;
				camel_dlist_remove((CamelDListNode *)ic);
				imapx_command_start(is, ic);
				count++;
			} else {
				/* This job isn't for the selected folder, but we don't want to
				   consider jobs with _lower_ priority than this, even if they
				   are for the selected folder. */
				pri = ic->pri;
			}
			ic = nc;
			nc = nc->next;
		}

		if (count)
			return;

		ic = (CamelIMAPXCommand *)is->queue.head;
	}

	/* If we need to select a folder for the first command, do it now, once
	   it is complete it will re-call us if it succeeded */
	if (ic->select) {
		c(printf("Selecting folder '%s' for command '%s'(%p)\n",
			 camel_folder_get_full_name(ic->select), ic->name, ic));
		imapx_select(is, ic->select, FALSE, error);
	} else {
		pri = ic->pri;
		nc = ic->next;
		count = 0;
		while (nc && is->literal == NULL && count < MAX_COMMANDS && ic->pri >= pri) {
			if (!ic->select || (ic->select == is->select_folder &&
					    !duplicate_fetch_or_refresh(is, ic))) {
				c(printf("* queueing job %3d '%s'\n", (gint)ic->pri, ic->name));
				pri = ic->pri;
				camel_dlist_remove((CamelDListNode *)ic);
				imapx_command_start(is, ic);
				count++;
			}
			ic = nc;
			nc = nc->next;
		}
	}
}

static gboolean
imapx_is_command_queue_empty (CamelIMAPXServer *is)
{
	gboolean ret = FALSE;

	if (camel_dlist_empty (&is->queue) && camel_dlist_empty (&is->active))
		ret = TRUE;

	return ret;
}

static void
imapx_command_queue(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXCommand *scan;

	/* We enqueue in priority order, new messages have
	   higher priority than older messages with the same priority */

	camel_imapx_command_close(ic);

	c(printf("enqueue job '%.*s'\n", ((CamelIMAPXCommandPart *)ic->parts.head)->data_size, ((CamelIMAPXCommandPart *)ic->parts.head)->data));

	QUEUE_LOCK(is);

	if (is->state == IMAPX_SHUTDOWN) {
		c(printf("refuse to queue job on disconnected server\n"));
		g_set_error (
			&ic->error, CAMEL_IMAPX_ERROR, 1,
			"Server disconnected");
		QUEUE_UNLOCK(is);
		if (ic->complete)
			ic->complete(is, ic);
		return;
	}

	scan = (CamelIMAPXCommand *)is->queue.head;
	if (scan->next == NULL)
		camel_dlist_addtail(&is->queue, (CamelDListNode *)ic);
	else {
		while (scan->next) {
			if (ic->pri >= scan->pri)
				break;
			scan = scan->next;
		}

		scan->prev->next = ic;
		ic->next = scan;
		ic->prev = scan->prev;
		scan->prev = ic;
	}

	imapx_command_start_next (is, NULL);

	QUEUE_UNLOCK(is);

	return;
}

/* Must have QUEUE lock */
static CamelIMAPXCommand *
imapx_find_command_tag(CamelIMAPXServer *imap, guint tag)
{
	CamelIMAPXCommand *ic = NULL;

	QUEUE_LOCK (imap);

	ic = imap->literal;
	if (ic && ic->tag == tag)
		goto found;

	for (ic = (CamelIMAPXCommand *)imap->active.head;ic->next;ic=ic->next)
		if (ic->tag == tag)
			goto found;

	/* Not found: force it to NULL otherwise we return the tail address */
	ic = NULL;

found:
	QUEUE_UNLOCK (imap);

	return ic;
}

static gboolean
imapx_job_matches (CamelFolder *folder, CamelIMAPXJob *job, guint32 type, const gchar *uid)
{
	switch (job->type) {
		case IMAPX_JOB_GET_MESSAGE:
			if (folder == job->folder &&
			    strcmp(job->u.get_message.uid, uid) == 0)
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
imapx_match_active_job (CamelIMAPXServer *is, guint32 type, const gchar *uid)
{
	CamelIMAPXJob *job = NULL;
	CamelIMAPXCommand *ic;

	QUEUE_LOCK(is);

	for (ic = (CamelIMAPXCommand *)is->active.head;ic->next;ic=ic->next) {
		job = ic->job;
		if (!job || !(job->type & type))
			continue;

		if (imapx_job_matches (is->select_folder, job, type, uid))
			goto found;
	}
	job = NULL;
found:
	QUEUE_UNLOCK(is);
	return job;
}

static CamelIMAPXJob *
imapx_is_job_in_queue (CamelIMAPXServer *is, CamelFolder *folder, guint32 type, const gchar *uid)
{
	CamelDListNode *node;
	CamelIMAPXJob *job = NULL;
	gboolean found = FALSE;

	QUEUE_LOCK(is);

	for (node = is->jobs.head;node->next;node = job->msg.ln.next) {
		job = (CamelIMAPXJob *) node;

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
imapx_expunge_uid_from_summary(CamelIMAPXServer *imap, gchar *uid, gboolean unsolicited)
{
	CamelMessageInfo *mi;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *)imap->select_folder;

	if (unsolicited && ifolder->exists_on_server)
		ifolder->exists_on_server--;

	if (imap->changes == NULL)
		imap->changes = camel_folder_change_info_new();

	mi = camel_folder_summary_uid (imap->select_folder->summary, uid);
	if (mi) {
		imapx_update_summary_for_removed_message (mi, imap->select_folder, unsolicited);
		camel_message_info_free (mi);
	}

	camel_folder_summary_remove_uid_fast (imap->select_folder->summary, uid);
	imap->expunged = g_slist_prepend (imap->expunged, uid);

	camel_folder_change_info_remove_uid (imap->changes, uid);

	if (imapx_idle_supported (imap) && imapx_in_idle (imap)) {
		const gchar *full_name;

		full_name = camel_folder_get_full_name (imap->select_folder);
		camel_db_delete_uids (imap->store->cdb_w, full_name, imap->expunged, NULL);
		imapx_update_store_summary (imap->select_folder);
		camel_folder_changed (imap->select_folder, imap->changes);

		g_slist_foreach (imap->expunged, (GFunc) g_free, NULL);
		g_slist_free (imap->expunged);
		imap->expunged = NULL;
		camel_folder_change_info_clear (imap->changes);
	}
}

/* handle any untagged responses */
static gint
imapx_untagged(CamelIMAPXServer *imap, GError **error)
{
	guint id, len;
	guchar *token, *p, c;
	gint tok;
	gboolean lsub = FALSE;
	struct _status_info *sinfo;

	e(printf("got untagged response\n"));
	id = 0;
	tok = camel_imapx_stream_token(imap->stream, &token, &len, error);
	if (tok < 0)
		return -1;

	if (tok == IMAPX_TOK_INT) {
		id = strtoul((gchar *) token, NULL, 10);
		tok = camel_imapx_stream_token(imap->stream, &token, &len, error);
		if (tok < 0)
			return -1;
	}

	if (tok == '\n') {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"truncated server response");
		return -1;
	}

	e(printf("Have token '%s' id %d\n", token, id));
	p = token;
	while ((c = *p))
		*p++ = toupper((gchar) c);

	switch (imapx_tokenise ((const gchar *) token, len)) {
	case IMAPX_CAPABILITY:
		if (imap->cinfo)
			imapx_free_capability(imap->cinfo);
		imap->cinfo = imapx_parse_capability(imap->stream, error);
		if (imap->cinfo == NULL)
			return -1;
		c(printf("got capability flags %08x\n", imap->cinfo->capa));
		return 0;
	case IMAPX_EXPUNGE: {
		guint32 expunge = id;
		CamelIMAPXJob *job = imapx_match_active_job (imap, IMAPX_JOB_EXPUNGE, NULL);

		/* If there is a job running, let it handle the deletion */
		if (job)
			break;

		c(printf("expunged: %d\n", id));
		if (imap->select_folder) {
			gchar *uid = NULL;

			uid = camel_folder_summary_uid_from_index (imap->select_folder->summary, expunge - 1);
			if (!uid)
				break;

			imapx_expunge_uid_from_summary(imap, uid, TRUE);
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

		tok = camel_imapx_stream_token (imap->stream, &token, &len, error);
		if (tok < 0)
			return -1;
		if (tok == '(') {
			unsolicited = FALSE;
			while (tok != ')') {
				/* We expect this to be 'EARLIER' */
				tok = camel_imapx_stream_token(imap->stream, &token, &len, error);
				if (tok < 0)
					return -1;
			}
		} else
			camel_imapx_stream_ungettoken(imap->stream, tok, token, len);

		uids = imapx_parse_uids(imap->stream, error);
		if (uids == NULL)
			return -1;
		for (i = 0; i < uids->len; i++) {
			gchar *uid = g_strdup_printf("%u", GPOINTER_TO_UINT(g_ptr_array_index (uids, i)));
			c(printf("vanished: %s\n", uid));
			imapx_expunge_uid_from_summary(imap, uid, unsolicited);
		}
		g_ptr_array_free (uids, FALSE);
		break;
	}
	case IMAPX_NAMESPACE: {
		CamelIMAPXNamespaceList *nsl = NULL;

		nsl = imapx_parse_namespace_list (imap->stream, error);
		if (nsl != NULL) {
			CamelIMAPXStore *imapx_store = (CamelIMAPXStore *) imap->store;
			CamelIMAPXStoreNamespace *ns;

			imapx_store->summary->namespaces = nsl;
			camel_store_summary_touch ((CamelStoreSummary *) imapx_store->summary);

			/* TODO Need to remove imapx_store->dir_sep to support multiple namespaces */
			ns = nsl->personal;
			if (ns)
				imapx_store->dir_sep = ns->sep;
		}

		return 0;
	}
	case IMAPX_EXISTS:
		c(printf("exists: %d\n", id));
		imap->exists = id;

		if (imap->select_folder)
			((CamelIMAPXFolder *) imap->select_folder)->exists_on_server = id;

		if (imapx_idle_supported (imap) && imapx_in_idle (imap)) {
			if (camel_folder_summary_count (imap->select_folder->summary) < id)
				imapx_stop_idle (imap, error);
		}

		break;
	case IMAPX_FLAGS: {
		guint32 flags;

		imapx_parse_flags(imap->stream, &flags, NULL, error);

		c(printf("flags: %08x\n", flags));
		break;
	}
	case IMAPX_FETCH: {
		struct _fetch_info *finfo;

		finfo = imapx_parse_fetch(imap->stream, error);
		if (finfo == NULL) {
			imapx_free_fetch(finfo);
			return -1;
		}

		if ((finfo->got & (FETCH_BODY|FETCH_UID)) == (FETCH_BODY|FETCH_UID)) {
			CamelIMAPXJob *job = imapx_match_active_job(imap, IMAPX_JOB_GET_MESSAGE, finfo->uid);

			/* This must've been a get-message request, fill out the body stream,
			   in the right spot */

			if (job && job->error == NULL) {
				if (job->u.get_message.use_multi_fetch) {
					job->u.get_message.body_offset = finfo->offset;
					camel_seekable_stream_seek((CamelSeekableStream *)job->u.get_message.stream, finfo->offset, CAMEL_STREAM_SET, NULL);
				}

				job->u.get_message.body_len = camel_stream_write_to_stream(finfo->body, job->u.get_message.stream, &job->error);
				if (job->u.get_message.body_len == -1)
					g_prefix_error (
						&job->error,
						_("Error writing to cache stream: "));
			}
		}

		if ((finfo->got & FETCH_FLAGS) && !(finfo->got & FETCH_HEADER)) {
			CamelIMAPXJob *job = imapx_match_active_job (imap, IMAPX_JOB_FETCH_NEW_MESSAGES|IMAPX_JOB_REFRESH_INFO, NULL);
			/* This is either a refresh_info job, check to see if it is and update
			   if so, otherwise it must've been an unsolicited response, so update
			   the summary to match */

			if (job && (finfo->got & FETCH_UID)) {
				struct _refresh_info r;

				r.uid = finfo->uid;
				finfo->uid = NULL;
				r.server_flags = finfo->flags;
				r.server_user_flags = finfo->user_flags;
				finfo->user_flags = NULL;
				r.exists = FALSE;
				g_array_append_val(job->u.refresh_info.infos, r);
			} else if (imap->select_folder) {
				CamelFolder *folder;
				CamelMessageInfo *mi = NULL;
				gboolean changed = FALSE;
				gchar *uid = NULL;

				g_object_ref (imap->select_folder);
				folder = imap->select_folder;

				c(printf("flag changed: %d\n", id));

				if (finfo->got & FETCH_UID) {
					uid = finfo->uid;
					finfo->uid = NULL;
				} else {
					uid = camel_folder_summary_uid_from_index (folder->summary, id - 1);
				}

				if (uid) {
					mi = camel_folder_summary_uid (folder->summary, uid);
					if (mi) {
						/* It's unsolicited _unless_ imap->select_pending (i.e. during
						   a QRESYNC SELECT */
						changed = imapx_update_message_info_flags (mi, finfo->flags, finfo->user_flags, folder, !imap->select_pending);
					} else {
						/* This (UID + FLAGS for previously unknown message) might
						   happen during a SELECT (QRESYNC). We should use it. */
						c(printf("flags changed for unknown uid %s\n.", uid));
					}
					finfo->user_flags = NULL;
				}

				if (changed) {
					if (imap->changes == NULL)
						imap->changes = camel_folder_change_info_new();

					camel_folder_change_info_change_uid(imap->changes, uid);
					g_free (uid);
				}

				if (imapx_idle_supported (imap) && changed && imapx_in_idle (imap)) {
					camel_folder_summary_save_to_db (imap->select_folder->summary, NULL);
					imapx_update_store_summary (imap->select_folder);
					camel_folder_changed (imap->select_folder, imap->changes);
					camel_folder_change_info_clear (imap->changes);
				}

				if (mi)
					camel_message_info_free (mi);
				g_object_unref (folder);
			}
		}

		if ((finfo->got & (FETCH_HEADER|FETCH_UID)) == (FETCH_HEADER|FETCH_UID)) {
			CamelIMAPXJob *job = imapx_match_active_job (imap, IMAPX_JOB_FETCH_NEW_MESSAGES|IMAPX_JOB_REFRESH_INFO, NULL);

			/* This must be a refresh info job as well, but it has asked for
			   new messages to be added to the index */

			if (job) {
				CamelMimeParser *mp;
				CamelMessageInfo *mi;

				/* Do we want to save these headers for later too?  Do we care? */

				mp = camel_mime_parser_new();
				camel_mime_parser_init_with_stream(mp, finfo->header, NULL);
				mi = camel_folder_summary_info_new_from_parser(job->folder->summary, mp);
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

							mid = (min + max)/2;
							r = &g_array_index(infos, struct _refresh_info, mid);
							cmp = imapx_uid_cmp (finfo->uid, r->uid, NULL);

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
					   we know as UIDNEXT for the folder, then it came in since we last
					   fetched UIDNEXT and UNREAD count. We'll update UIDNEXT in the
					   command completion, but update UNREAD count now according to the
					   message SEEN flag */
					if (!(server_flags & CAMEL_MESSAGE_SEEN)) {
						CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *)job->folder;
						unsigned long long uidl = strtoull(mi->uid, NULL, 10);

						if (uidl >= ifolder->uidnext_on_server) {
							c(printf("Updating unread count for new message %s\n", mi->uid));
							((CamelIMAPXFolder *)job->folder)->unread_on_server++;
						} else {
							c(printf("Not updating unread count for new message %s\n", mi->uid));
						}
					}

					binfo = (CamelMessageInfoBase *) mi;
					binfo->size = finfo->size;

					if (!camel_folder_summary_check_uid (job->folder->summary, mi->uid)) {
						CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *)job->folder;

						camel_folder_summary_add(job->folder->summary, mi);
						imapx_set_message_info_flags_for_new_message (mi, server_flags, server_user_flags, job->folder);
						camel_folder_change_info_add_uid (job->u.refresh_info.changes, mi->uid);

						if (!g_hash_table_lookup (ifolder->ignore_recent, mi->uid)) {
							camel_folder_change_info_recent_uid (job->u.refresh_info.changes, mi->uid);
							g_hash_table_remove (ifolder->ignore_recent, mi->uid);
						}

						if (job->op) {
							gint cnt = (camel_folder_summary_count (job->folder->summary) * 100 )/ifolder->exists_on_server;
							camel_operation_progress (job->op, cnt?cnt:1);
						}
					}

					if (free_user_flags && server_user_flags)
						camel_flag_list_free (&server_user_flags);

				}
			}
		}

		imapx_free_fetch(finfo);
		break;
	}
	case IMAPX_LSUB:
		lsub = TRUE;
	case IMAPX_LIST: {
		struct _list_info *linfo = imapx_parse_list(imap->stream, error);
		CamelIMAPXJob *job;

		if (!linfo)
			break;

		job = imapx_match_active_job(imap, IMAPX_JOB_LIST, linfo->name);

		// TODO: we want to make sure the names match?

		if (job->u.list.flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) {
			c(printf("lsub: '%s' (%c)\n", linfo->name, linfo->separator));

		} else {
			c(printf("list: '%s' (%c)\n", linfo->name, linfo->separator));
		}

		if (job && g_hash_table_lookup(job->u.list.folders, linfo->name) == NULL) {
			if (lsub)
				linfo->flags |= CAMEL_FOLDER_SUBSCRIBED;
			g_hash_table_insert(job->u.list.folders, linfo->name, linfo);
		} else {
			g_warning("got list response but no current listing job happening?\n");
			imapx_free_list(linfo);
		}
		break;
	}
	case IMAPX_RECENT:
		c(printf("recent: %d\n", id));
		imap->recent = id;
		break;
	case IMAPX_STATUS: {
		struct _state_info *sinfo = imapx_parse_status_info (imap->stream, error);
		if (sinfo) {
			CamelIMAPXStoreSummary *s = ((CamelIMAPXStore *)imap->store)->summary;
			CamelIMAPXStoreNamespace *ns;
			CamelIMAPXFolder *ifolder = NULL;;

			ns = camel_imapx_store_summary_namespace_find_full(s, sinfo->name);
			if (ns) {
				gchar *path_name;

				path_name = camel_imapx_store_summary_full_to_path(s, sinfo->name, ns->sep);
				c(printf("Got folder path '%s' for full '%s'\n", path_name, sinfo->name));
				if (path_name) {
					ifolder = (gpointer)camel_store_get_folder(imap->store, path_name, 0, error);
					g_free (path_name);
				}
			}
			if (ifolder) {
				ifolder->unread_on_server = sinfo->unseen;
				ifolder->exists_on_server = sinfo->messages;
				ifolder->modseq_on_server = sinfo->highestmodseq;
				ifolder->uidnext_on_server = sinfo->uidnext;
				ifolder->uidvalidity_on_server = sinfo->uidvalidity;
			} else {
				c(printf("Received STATUS for unknown folder '%s'\n", sinfo->name));
			}

			g_free (sinfo->name);
			g_free (sinfo);
		}
		break;
	}
	case IMAPX_BYE: {
		guchar *token;

		if (camel_imapx_stream_text (imap->stream, &token, NULL)) {
			c(printf("BYE: %s\n", token));
			g_set_error (
				error, CAMEL_IMAPX_ERROR, 1,
				"IMAP server said BYE: %s", token);
		}
		imap->state = IMAPX_SHUTDOWN;
		return -1;
	}
	case IMAPX_PREAUTH:
		c(printf("preauthenticated\n"));
		if (imap->state < IMAPX_AUTHENTICATED)
			imap->state = IMAPX_AUTHENTICATED;
		/* fall through... */
	case IMAPX_OK: case IMAPX_NO: case IMAPX_BAD:
		/* TODO: validate which ones of these can happen as unsolicited responses */
		/* TODO: handle bye/preauth differently */
		camel_imapx_stream_ungettoken(imap->stream, tok, token, len);
		sinfo = imapx_parse_status(imap->stream, error);
		if (sinfo == NULL)
			return -1;
		switch (sinfo->condition) {
		case IMAPX_CLOSED:
			c(printf("previously selected folder is now closed\n"));
			if (imap->select_pending && !imap->select_folder) {
				imap->select_folder = imap->select_pending;
			}
			break;
		case IMAPX_READ_WRITE:
			imap->mode = IMAPX_MODE_READ|IMAPX_MODE_WRITE;
			c(printf("folder is read-write\n"));
			break;
		case IMAPX_READ_ONLY:
			imap->mode = IMAPX_MODE_READ;
			c(printf("folder is read-only\n"));
			break;
		case IMAPX_UIDVALIDITY:
			imap->uidvalidity = sinfo->u.uidvalidity;
			break;
		case IMAPX_UNSEEN:
			imap->unseen = sinfo->u.unseen;
			break;
		case IMAPX_HIGHESTMODSEQ:
			imap->highestmodseq = sinfo->u.highestmodseq;
			break;
		case IMAPX_PERMANENTFLAGS:
			imap->permanentflags = sinfo->u.permanentflags;
			break;
		case IMAPX_UIDNEXT:
			imap->uidnext = sinfo->u.uidnext;
			break;
		case IMAPX_ALERT:
			c(printf("ALERT!: %s\n", sinfo->text));
			break;
		case IMAPX_PARSE:
			c(printf("PARSE: %s\n", sinfo->text));
			break;
		case IMAPX_CAPABILITY:
			if (sinfo->u.cinfo) {
				struct _capability_info *cinfo = imap->cinfo;
				imap->cinfo = sinfo->u.cinfo;
				sinfo->u.cinfo = NULL;
				if (cinfo)
					imapx_free_capability(cinfo);
				c(printf("got capability flags %08x\n", imap->cinfo->capa));
			}
			break;
		default:
			break;
		}
		imapx_free_status(sinfo);
		return 0;
	default:
		/* unknown response, just ignore it */
		c(printf("unknown token: %s\n", token));
	}

	return camel_imapx_stream_skip(imap->stream, error);
}

/* handle any continuation requests
   either data continuations, or auth continuation */
static gint
imapx_continuation(CamelIMAPXServer *imap, gboolean litplus, GError **error)
{
	CamelIMAPXCommand *ic, *newliteral = NULL;
	CamelIMAPXCommandPart *cp;

	/* The 'literal' pointer is like a write-lock, nothing else
	   can write while we have it ... so we dont need any
	   ohter lock here.  All other writes go through
	   queue-lock */
	if (imapx_idle_supported (imap) && imapx_in_idle (imap)) {
		camel_imapx_stream_skip (imap->stream, error);

		c(printf("Got continuation response for IDLE \n"));
		IDLE_LOCK(imap->idle);
		/* We might have actually sent the DONE already! */
		if (imap->idle->state == IMAPX_IDLE_ISSUED)
			imap->idle->state = IMAPX_IDLE_STARTED;
		else if (imap->idle->state == IMAPX_IDLE_CANCEL) {
			/* IDLE got cancelled after we sent the command, while
			   we were waiting for this continuation. Send DONE
			   immediately. */
			if (!imapx_command_idle_stop (imap, error)) {
				IDLE_UNLOCK(imap->idle);
				return -1;
			}
			imap->idle->state = IMAPX_IDLE_OFF;
		} else {
			c(printf("idle starts in wrong state %d\n",
				 imap->idle->state));
		}
		IDLE_UNLOCK(imap->idle);

		QUEUE_LOCK(imap);
		imap->literal = NULL;
		imapx_command_start_next(imap, error);
		QUEUE_UNLOCK(imap);

		return 1;
	}

	ic = imap->literal;
	if (!litplus) {
		if (ic == NULL) {
			camel_imapx_stream_skip(imap->stream, error);
			c(printf("got continuation response with no outstanding continuation requests?\n"));
			return 1;
		}
		c(printf("got continuation response for data\n"));
	} else {
		c(printf("sending LITERAL+ continuation\n"));
	}

	cp = ic->current;
	switch (cp->type & CAMEL_IMAPX_COMMAND_MASK) {
	case CAMEL_IMAPX_COMMAND_DATAWRAPPER:
		c(printf("writing data wrapper to literal\n"));
		camel_data_wrapper_write_to_stream((CamelDataWrapper *)cp->ob, (CamelStream *)imap->stream, NULL);
		break;
	case CAMEL_IMAPX_COMMAND_STREAM:
		c(printf("writing stream to literal\n"));
		camel_stream_write_to_stream((CamelStream *)cp->ob, (CamelStream *)imap->stream, NULL);
		break;
	case CAMEL_IMAPX_COMMAND_AUTH: {
		gchar *resp;
		guchar *token;

		if (camel_imapx_stream_text (imap->stream, &token, error))
			return -1;

		resp = camel_sasl_challenge_base64((CamelSasl *)cp->ob, (const gchar *) token, error);
		g_free(token);
		if (resp == NULL)
			return -1;
		c(printf("got auth continuation, feeding token '%s' back to auth mech\n", resp));

		camel_stream_write((CamelStream *)imap->stream, resp, strlen(resp), NULL);
		g_free(resp);
		/* we want to keep getting called until we get a status reponse from the server
		   ignore what sasl tells us */
		newliteral = ic;
		/* We already ate the end of the input stream line */
		goto noskip;
		break; }
	case CAMEL_IMAPX_COMMAND_FILE: {
		CamelStream *file;

		c(printf("writing file '%s' to literal\n", (gchar *)cp->ob));

		// FIXME: errors
		if (cp->ob && (file = camel_stream_fs_new_with_name(cp->ob, O_RDONLY, 0, NULL))) {
			camel_stream_write_to_stream(file, (CamelStream *)imap->stream, NULL);
			g_object_unref (file);
		} else if (cp->ob_size > 0) {
			// Server is expecting data ... ummm, send it zeros?  abort?
		}
		break; }
	case CAMEL_IMAPX_COMMAND_STRING:
		camel_stream_write((CamelStream *)imap->stream, cp->ob, cp->ob_size, NULL);
		break;
	default:
		/* should we just ignore? */
		imap->literal = NULL;
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"continuation response for non-continuation request");
		return -1;
	}

	if (!litplus)
		camel_imapx_stream_skip(imap->stream, error);
 noskip:
	cp = cp->next;
	if (cp->next) {
		ic->current = cp;
		c(printf("next part of command \"%c%05u: %s\"\n", imap->tagprefix, ic->tag, cp->data));
		camel_stream_printf((CamelStream *)imap->stream, "%s\r\n", cp->data);
		if (cp->type & (CAMEL_IMAPX_COMMAND_CONTINUATION|CAMEL_IMAPX_COMMAND_LITERAL_PLUS)) {
			newliteral = ic;
		} else {
			g_assert(cp->next->next == NULL);
		}
	} else {
		c(printf("%p: queueing continuation\n", ic));
		camel_stream_printf((CamelStream *)imap->stream, "\r\n");
	}

	QUEUE_LOCK(imap);
	imap->literal = newliteral;

	if (!litplus)
		imapx_command_start_next(imap, error);
	QUEUE_UNLOCK(imap);

	return 1;
}

/* handle a completion line */
static gint
imapx_completion(CamelIMAPXServer *imap, guchar *token, gint len, GError **error)
{
	CamelIMAPXCommand *ic;
	guint tag;

	if (token[0] != imap->tagprefix) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"Server sent unexpected response: %s", token);

		return -1;
	}

	tag = strtoul( (const gchar *)token+1, NULL, 10);

	if ((ic = imapx_find_command_tag(imap, tag)) == NULL) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"got response tag unexpectedly: %s", token);

		return -1;
	}

	c(printf("Got completion response for command %05u '%s'\n", ic->tag, ic->name));

	if (camel_folder_change_info_changed (imap->changes)) {
		if (imap->changes->uid_changed->len)
			camel_folder_summary_save_to_db (imap->select_folder->summary, NULL);
		else {
			const gchar *full_name;

			full_name = camel_folder_get_full_name (imap->select_folder);
			camel_db_delete_uids (imap->store->cdb_w, full_name, imap->expunged, NULL);
		}

		if (imap->expunged) {
			g_slist_foreach (imap->expunged, (GFunc) g_free, NULL);
			g_slist_free (imap->expunged);
			imap->expunged = NULL;
		}

		imapx_update_store_summary (imap->select_folder);
		camel_folder_changed (imap->select_folder, imap->changes);
		camel_folder_change_info_clear (imap->changes);
	}

	QUEUE_LOCK(imap);

	camel_dlist_remove((CamelDListNode *)ic);
	camel_dlist_addtail(&imap->done, (CamelDListNode *)ic);
	if (imap->literal == ic)
		imap->literal = NULL;

	if (ic->current->next->next) {
		QUEUE_UNLOCK(imap);
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"command still has unsent parts? %s", ic->name);

		return -1;
	}

	camel_dlist_remove ((CamelDListNode *) ic);
	QUEUE_UNLOCK(imap);

	ic->status = imapx_parse_status(imap->stream, error);

	if (ic->complete)
		ic->complete (imap, ic);

	QUEUE_LOCK(imap);
	imapx_command_start_next(imap, error);
	QUEUE_UNLOCK(imap);

	return 1;
}

static void
imapx_step(CamelIMAPXServer *is, GError **error)
{
	guint len;
	guchar *token;
	gint tok;

	// poll ?  wait for other stuff? loop?
	tok = camel_imapx_stream_token (is->stream, &token, &len, error);
	if (tok < 0)
		return;

	if (tok == '*')
		imapx_untagged (is, error);
	else if (tok == IMAPX_TOK_TOKEN)
		imapx_completion (is, token, len, error);
	else if (tok == '+')
		imapx_continuation (is, FALSE, error);
	else
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"unexpected server response:");
}

/* Used to run 1 command synchronously,
   use for capa, login, and namespaces only. */
static gboolean
imapx_command_run (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
/* throws IO,PARSE exception */
{
	camel_imapx_command_close(ic);

	QUEUE_LOCK(is);
	imapx_command_start(is, ic);
	QUEUE_UNLOCK(is);

	while (ic->status == NULL && ic->error == NULL)
		imapx_step(is, &ic->error);

	if (is->literal == ic)
		is->literal = NULL;

	QUEUE_LOCK(is);
	camel_dlist_remove((CamelDListNode *)ic);
	QUEUE_UNLOCK(is);

	return (ic->error == NULL);
}

static void
imapx_command_complete (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	e_flag_set (ic->flag);
}

/* The caller should free the command as well */
static void
imapx_command_run_sync (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXCommandFunc complete = NULL;

	ic->flag = e_flag_new ();
	complete = ic->complete;

	if (!ic->complete)
		ic->complete = imapx_command_complete;

	imapx_command_queue (is, ic);
	e_flag_wait (ic->flag);

	e_flag_free (ic->flag);
	ic->flag = NULL;
}

/* ********************************************************************** */
/* Should be called when there are no more commands needed to complete the job */

static void
imapx_job_done (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	QUEUE_LOCK (is);
	camel_dlist_remove((CamelDListNode *)job);
	QUEUE_UNLOCK (is);

	if (job->noreply) {
		g_clear_error (&job->error);
		g_free(job);
	} else
		camel_msgport_reply((CamelMsg *) job);
}

static gboolean
imapx_register_job (CamelIMAPXServer *is,
                    CamelIMAPXJob *job,
                    GError **error)
{
	if (is->state >= IMAPX_INITIALISED) {
		QUEUE_LOCK (is);
		camel_dlist_addhead (&is->jobs, (CamelDListNode *)job);
		QUEUE_UNLOCK (is);

	} else {
		e(printf ("NO connection yet, maybe user cancelled jobs earlier ?"));
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
	CamelMsgPort *reply = NULL;

	if (!job->noreply) {
		reply = camel_msgport_new ();
		job->msg.reply_port = reply;
	}

	/* Any exceptions to the start should be reported async through our reply msgport */
	job->start (is, job);

	if (!job->noreply) {
		CamelMsg *completed;

		completed = camel_msgport_pop (reply);
		camel_msgport_destroy (reply);

		g_assert(completed == (CamelMsg *)job);
	}

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
imapx_command_idle_stop (CamelIMAPXServer *is, GError **error)
{
	if (!is->stream || camel_stream_printf((CamelStream *)is->stream, "%s", "DONE\r\n") == -1) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"Unable to issue DONE");
		c(printf("Failed to issue DONE to terminate IDLE\n"));
		is->state = IMAPX_SHUTDOWN;
		if (is->op)
			camel_operation_cancel(is->op);
		is->parser_quit = TRUE;
		return FALSE;
	}

	return TRUE;
}

static void
imapx_command_idle_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXIdle *idle = is->idle;

	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&ic->job->error, CAMEL_IMAPX_ERROR, 1,
				"Error performing IDLE: %s", ic->status->text);
		else {
			g_propagate_error (&ic->job->error, ic->error);
			ic->error = NULL;
		}
	}

	IDLE_LOCK (idle);
	idle->state = IMAPX_IDLE_OFF;
	IDLE_UNLOCK (idle);

	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_idle_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXCommandPart *cp;
	const gchar *full_name;

	full_name = camel_folder_get_full_name (job->folder);

	ic = camel_imapx_command_new (is, "IDLE", job->folder, "IDLE");
	ic->job = job;
	ic->pri = job->pri;
	ic->complete = imapx_command_idle_done;

	camel_imapx_command_close(ic);
	cp = (CamelIMAPXCommandPart *)ic->parts.head;
	cp->type |= CAMEL_IMAPX_COMMAND_CONTINUATION;

	QUEUE_LOCK (is);
	IDLE_LOCK(is->idle);
	/* Don't issue it if the idle was cancelled already */
	if (is->idle->state == IMAPX_IDLE_PENDING) {
		is->idle->state = IMAPX_IDLE_ISSUED;
		imapx_command_start (is, ic);
	} else {
		imapx_job_done (is, ic->job);
		camel_imapx_command_free (ic);
	}
	IDLE_UNLOCK(is->idle);
	QUEUE_UNLOCK (is);
}

static gboolean
camel_imapx_server_idle (CamelIMAPXServer *is, CamelFolder *folder, GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = g_malloc0 (sizeof(*job));
	job->type = IMAPX_JOB_IDLE;
	job->start = imapx_job_idle_start;
	job->folder = folder;

	success = imapx_submit_job (is, job, error);

	g_free(job);

	return success;
}

static gboolean
imapx_server_fetch_new_messages (CamelIMAPXServer *is,
                                 CamelFolder *folder,
                                 gboolean async,
                                 gboolean update_unseen,
                                 GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_FETCH_NEW_MESSAGES;
	job->start = imapx_job_fetch_new_messages_start;
	job->folder = folder;
	job->noreply = async;
	job->u.refresh_info.changes = camel_folder_change_info_new();
	job->u.refresh_info.update_unseen = update_unseen;
	job->op = camel_operation_registered ();

	success = imapx_submit_job (is, job, error);

	if (!async)
		g_free (job);

	return success;
}

static gpointer
imapx_idle_thread (gpointer data)
{
	CamelIMAPXServer *is = (CamelIMAPXServer *) data;
	GError *local_error = NULL;

	while (TRUE) {
		CamelIMAPXFolder *ifolder;

		e_flag_clear (is->idle->idle_start_watch);

		IDLE_LOCK(is->idle);
		while ((ifolder = (CamelIMAPXFolder *) is->select_folder) &&
		       is->idle->state == IMAPX_IDLE_PENDING &&
		       !is->idle->idle_exit) {
			time_t dwelled = time(NULL) - is->idle->started;

			if (dwelled < IMAPX_IDLE_DWELL_TIME) {
				IDLE_UNLOCK(is->idle);
				g_usleep((IMAPX_IDLE_DWELL_TIME - dwelled) * G_USEC_PER_SEC);
				IDLE_LOCK(is->idle);
				continue;
			}
			IDLE_UNLOCK(is->idle);
			camel_imapx_server_idle (is, (gpointer)ifolder, &local_error);

			if (local_error == NULL && ifolder->exists_on_server >
			    camel_folder_summary_count (((CamelFolder *) ifolder)->summary) && imapx_is_command_queue_empty (is))
				imapx_server_fetch_new_messages (is, is->select_folder, TRUE, TRUE, &local_error);

			if (local_error != NULL) {
				e(printf ("Caught exception in idle thread:  %s \n", local_error->message));
				/* No way to asyncronously notify UI ? */
				g_clear_error (&local_error);
			}
			IDLE_LOCK(is->idle);
		}
		IDLE_UNLOCK(is->idle);

		e_flag_wait (is->idle->idle_start_watch);

		if (is->idle->idle_exit)
			break;
	}

	g_clear_error (&local_error);
	is->idle->idle_thread = NULL;
	return NULL;
}

static gboolean
imapx_stop_idle (CamelIMAPXServer *is, GError **error)
{
	CamelIMAPXIdle *idle = is->idle;
	gint stopped = FALSE;
	time_t now;

	time(&now);
	IDLE_LOCK (idle);

	switch (idle->state) {
	case IMAPX_IDLE_ISSUED:
		idle->state = IMAPX_IDLE_CANCEL;
	case IMAPX_IDLE_CANCEL:
		stopped = TRUE;
		break;

	case IMAPX_IDLE_STARTED:
		/* We set 'stopped' even if sending DONE fails, to ensure that
		   our caller doesn't try to submit its own command. */
		stopped = TRUE;
		if (!imapx_command_idle_stop (is, error))
			break;

		idle->state = IMAPX_IDLE_OFF;
		c(printf("Stopping idle after %ld seconds\n",
			 (long)(now - idle->started)));
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
		e_flag_set (idle->idle_start_watch);

		thread = idle->idle_thread;
		idle->idle_thread = 0;
	}

	idle->idle_thread = NULL;
	IDLE_UNLOCK (idle);

	if (thread)
		g_thread_join (thread);

	g_mutex_free (idle->idle_lock);
	if (idle->idle_start_watch)
		e_flag_free (idle->idle_start_watch);

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
	time(&idle->started);
	idle->state = IMAPX_IDLE_PENDING;

	if (!idle->idle_thread) {
		idle->idle_start_watch = e_flag_new ();
		idle->idle_thread = g_thread_create ((GThreadFunc) imapx_idle_thread, is, TRUE, NULL);
	} else
		e_flag_set (idle->idle_start_watch);

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
static void
imapx_command_select_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	const gchar *selected_folder = NULL;

	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		CamelDList failed;
		CamelIMAPXCommand *cw, *cn;

		c(printf("Select failed\n"));
		camel_dlist_init (&failed);

		QUEUE_LOCK(is);
		cw = (CamelIMAPXCommand *)is->queue.head;
		cn = cw->next;

		if (is->select_pending) {
			while (cn) {
				if (cw->select && cw->select == is->select_pending) {
					c(printf("Cancelling command '%s'(%p) for folder '%s'\n",
						 cw->name, cw, camel_folder_get_full_name(cw->select)));
					camel_dlist_remove((CamelDListNode *)cw);
					camel_dlist_addtail(&failed, (CamelDListNode *)cw);
				}
				cw = cn;
				cn = cn->next;
			}
		}

		QUEUE_UNLOCK(is);

		cw = (CamelIMAPXCommand *)failed.head;
		if (cw) {
			cn = cw->next;
			while (cn) {
				if (ic->status)
					cw->status = imapx_copy_status(ic->status);
				if (ic->error != NULL) {
					g_propagate_error (&cw->error, ic->error);
					ic->error = NULL;
				} else
					g_set_error (
						&cw->error, CAMEL_IMAPX_ERROR, 1,
						"SELECT %s failed: %s",
						camel_folder_get_full_name(cw->select),
						ic->status->text? ic->status->text:"<unknown reason>");
				cw->complete(is, cw);
				cw = cn;
				cn = cn->next;
			}
		}
		if (is->select_pending)
			g_object_unref (is->select_pending);

		/* A [CLOSED] status may have caused us to assume that it had happened */
		if (is->select_folder)
			is->select_folder = NULL;

		is->state = IMAPX_INITIALISED;
	} else {
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) is->select_pending;
		c(printf("Select ok!\n"));

		if (!is->select_folder) {
			/* This could have been done earlier by a [CLOSED] status */
			is->select_folder = is->select_pending;
		}
		is->state = IMAPX_SELECTED;
		ifolder->exists_on_server = is->exists;
		ifolder->modseq_on_server = is->highestmodseq;
		if (ifolder->uidnext_on_server < is->uidnext) {
			imapx_server_fetch_new_messages (is, is->select_pending, TRUE, TRUE, NULL);
			/* We don't do this right now because we want the new messages to
			   update the unseen count. */
			//ifolder->uidnext_on_server = is->uidnext;
		}
		ifolder->uidvalidity_on_server = is->uidvalidity;
		selected_folder = camel_folder_get_full_name (is->select_folder);
#if 0
		/* This must trigger a complete index rebuild! */
		if (is->uidvalidity && is->uidvalidity != ((CamelIMAPXSummary *)is->select_folder->summary)->uidvalidity)
			g_warning("uidvalidity doesn't match!");

		/* This should trigger a new messages scan */
		if (is->exists != is->select_folder->summary->root_view->total_count)
			g_warning("exists is %d our summary is %d and summary exists is %d\n", is->exists,
				  is->select_folder->summary->root_view->total_count,
				  ((CamelIMAPXSummary *)is->select_folder->summary)->exists);
#endif
	}

	is->select_pending = NULL;
	camel_imapx_command_free (ic);

	g_signal_emit (is, signals[SELECT_CHANGED], 0, selected_folder);
}

/* Should have a queue lock. TODO Change the way select is written */
static gboolean
imapx_select (CamelIMAPXServer *is, CamelFolder *folder, gboolean forced, GError **error)
{
	CamelIMAPXCommand *ic;
	const gchar *full_name;

	full_name = camel_folder_get_full_name (folder);

	/* Select is complicated by the fact we may have commands
	   active on the server for a different selection.

	   So this waits for any commands to complete, selects the
	   new folder, and halts the queuing of any new commands.
	   It is assumed whomever called is us about to issue
	   a high-priority command anyway */

	/* TODO check locking here, pending_select will do
	   most of the work for normal commands, but not
	   for another select */

	if (is->select_pending)
		return TRUE;

	if (is->select_folder == folder && !forced)
		return TRUE;

	if (!camel_dlist_empty(&is->active))
		return TRUE;

	is->select_pending = folder;
	g_object_ref (folder);
	if (is->select_folder) {
		g_object_unref (is->select_folder);
		is->select_folder = NULL;
	} else {
		/* If no folder was selected, we won't get a [CLOSED] status
		   so just point select_folder at the new folder immediately */
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

	ic = camel_imapx_command_new(is, "SELECT", NULL, "SELECT %f", folder);

	if (is->use_qresync) {
		CamelIMAPXSummary *isum = (CamelIMAPXSummary *)folder->summary;
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *)folder;
		gint total = camel_folder_summary_count (folder->summary);
		gchar *firstuid, *lastuid;

		if (total && isum->modseq && ifolder->uidvalidity_on_server) {

			firstuid = camel_folder_summary_uid_from_index (folder->summary, 0);
			lastuid = camel_folder_summary_uid_from_index (folder->summary, total - 1);

			c(printf("SELECT QRESYNC %" G_GUINT64_FORMAT
				 " %" G_GUINT64_FORMAT "\n",
				 ifolder->uidvalidity_on_server, isum->modseq));
			camel_imapx_command_add(ic, " (QRESYNC (%"
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
				   (see RFC5162 §3.1). Work backwards exponentially from the
				   end of the mailbox, starting with the message 9 from the
				   end, then 27 from the end, then 81 from the end... */
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
					   indices in our folder summary -- they start from one, while
					   the summary starts from zero. */
					sprintf(buf, "%d", total - i + 1);
					g_string_prepend(seqs, buf);
					uid = camel_folder_summary_uid_from_index(folder->summary, total - i);
					g_string_prepend(uids, uid);
					g_free(uid);
				} while (i < total);

				g_string_prepend(seqs, " (");

				c(printf("adding QRESYNC seq/uidset %s%s\n", seqs->str, uids->str));
				camel_imapx_command_add(ic, seqs->str);
				camel_imapx_command_add(ic, uids->str);

				g_string_free(seqs, TRUE);
				g_string_free(uids, TRUE);

			}
			camel_imapx_command_add(ic, "))");
		}
	}

	ic->complete = imapx_command_select_done;
	imapx_command_start (is, ic);

	return TRUE;
}

#ifndef G_OS_WIN32

/* Using custom commands to connect to IMAP servers is not supported on Win32 */

static gboolean
connect_to_server_process (CamelIMAPXServer *is, const gchar *cmd, GError **error)
{
	CamelStream *cmd_stream;
	gint ret, i = 0;
	gchar *buf;
	gchar *cmd_copy;
	gchar *full_cmd;
	gchar *child_env[7];

	/* Put full details in the environment, in case the connection
	   program needs them */
	buf = camel_url_to_string(is->url, 0);
	child_env[i++] = g_strdup_printf("URL=%s", buf);
	g_free(buf);

	child_env[i++] = g_strdup_printf("URLHOST=%s", is->url->host);
	if (is->url->port)
		child_env[i++] = g_strdup_printf("URLPORT=%d", is->url->port);
	if (is->url->user)
		child_env[i++] = g_strdup_printf("URLUSER=%s", is->url->user);
	if (is->url->passwd)
		child_env[i++] = g_strdup_printf("URLPASSWD=%s", is->url->passwd);
	if (is->url->path)
		child_env[i++] = g_strdup_printf("URLPATH=%s", is->url->path);
	child_env[i] = NULL;

	/* Now do %h, %u, etc. substitution in cmd */
	buf = cmd_copy = g_strdup(cmd);

	full_cmd = g_strdup("");

	for (;;) {
		gchar *pc;
		gchar *tmp;
		gchar *var;
		gint len;

		pc = strchr(buf, '%');
	ignore:
		if (!pc) {
			tmp = g_strdup_printf("%s%s", full_cmd, buf);
			g_free(full_cmd);
			full_cmd = tmp;
			break;
		}

		len = pc - buf;

		var = NULL;

		switch (pc[1]) {
		case 'h':
			var = is->url->host;
			break;
		case 'u':
			var = is->url->user;
			break;
		}
		if (!var) {
			/* If there wasn't a valid %-code, with an actual
			   variable to insert, pretend we didn't see the % */
			pc = strchr(pc + 1, '%');
			goto ignore;
		}
		tmp = g_strdup_printf("%s%.*s%s", full_cmd, len, buf, var);
		g_free(full_cmd);
		full_cmd = tmp;
		buf = pc + 2;
	}

	g_free(cmd_copy);

	cmd_stream = camel_stream_process_new ();

	ret = camel_stream_process_connect (CAMEL_STREAM_PROCESS(cmd_stream),
					    full_cmd, (const gchar **)child_env);

	while (i)
		g_free(child_env[--i]);

	if (ret == -1) {
		if (errno == EINTR)
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED,
				_("Connection cancelled"));
		else
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (errno),
				_("Could not connect with command \"%s\": %s"),
				full_cmd, g_strerror (errno));

		g_object_unref (cmd_stream);
		g_free (full_cmd);
		return FALSE;
	}
	g_free (full_cmd);

	is->stream = (CamelIMAPXStream *) camel_imapx_stream_new(cmd_stream);
	g_object_unref(cmd_stream);
	is->is_process_stream = TRUE;

	return TRUE;
}
#endif /* G_OS_WIN32 */

gboolean
imapx_connect_to_server (CamelIMAPXServer *is, GError **error)
{
	CamelStream * tcp_stream = NULL;
	gchar *socks_host;
	gint socks_port;
	CamelSockOptData sockopt;
	gint ret, ssl_mode = 0;

#ifdef CAMEL_HAVE_SSL
	const gchar *mode;
#endif
	guint len;
	guchar *token;
	gint tok;
	const gchar *serv;
	gint fallback_port;
	CamelIMAPXCommand *ic;
	GError *local_error = NULL;

#ifndef G_OS_WIN32
	const gchar *command;

	if (camel_url_get_param(is->url, "use_command") &&
	    (command = camel_url_get_param(is->url, "command"))) {
		if (!connect_to_server_process(is, command, &local_error))
			goto exit;
		else
			goto connected;
	}
#endif
	if (is->url->port) {
		serv = g_alloca(16);
		sprintf((gchar *) serv, "%d", is->url->port);
		fallback_port = 0;
	} else {
		serv = "imap";
		fallback_port = 143;
	}
#ifdef CAMEL_HAVE_SSL
	mode = camel_url_get_param(is->url, "use_ssl");
	if (mode && strcmp(mode, "never") != 0) {
		if (!strcmp(mode, "when-possible")) {
			tcp_stream = camel_tcp_stream_ssl_new_raw(is->session, is->url->host, STARTTLS_FLAGS);
			ssl_mode = 2;
		} else {
			if (is->url->port == 0) {
				serv = "imaps";
				fallback_port = 993;
			}
			tcp_stream = camel_tcp_stream_ssl_new(is->session, is->url->host, SSL_PORT_FLAGS);
		}
	} else {
		tcp_stream = camel_tcp_stream_raw_new ();
	}
#else
	tcp_stream = camel_tcp_stream_raw_new ();
#endif /* CAMEL_HAVE_SSL */

	camel_session_get_socks_proxy (is->session, &socks_host, &socks_port);

	if (socks_host) {
		camel_tcp_stream_set_socks_proxy ((CamelTcpStream *) tcp_stream, socks_host, socks_port);
		g_free (socks_host);
	}

	ret = camel_tcp_stream_connect(CAMEL_TCP_STREAM(tcp_stream), is->url->host, serv, fallback_port, error);
	if (ret == -1) {
		g_prefix_error (
			error, _("Could not connect to %s (port %s): "),
			is->url->host, serv);
		g_object_unref (tcp_stream);
		return FALSE;
	}

	is->stream = (CamelIMAPXStream *) camel_imapx_stream_new(tcp_stream);
	g_object_unref (tcp_stream);

	/* Disable Nagle - we send a lot of small requests which nagle slows down */
	sockopt.option = CAMEL_SOCKOPT_NODELAY;
	sockopt.value.no_delay = TRUE;
	camel_tcp_stream_setsockopt((CamelTcpStream *)tcp_stream, &sockopt);

	/* Set keepalive - needed for some hosts/router configurations, we're idle a lot */
	sockopt.option = CAMEL_SOCKOPT_KEEPALIVE;
	sockopt.value.keep_alive = TRUE;
	camel_tcp_stream_setsockopt ((CamelTcpStream *)tcp_stream, &sockopt);

 connected:
	while (1) {
		// poll ?  wait for other stuff? loop?
		if (camel_application_is_exiting || is->parser_quit) {
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED,
				"Connection to server cancelled\n");
			return FALSE;
		}

		tok = camel_imapx_stream_token (is->stream, &token, &len, error);
		if (tok < 0)
			return FALSE;

		if (tok == '*') {
			imapx_untagged (is, error);
			break;
		}
		camel_imapx_stream_ungettoken(is->stream, tok, token, len);
		if (camel_imapx_stream_text (is->stream, &token, error))
			return FALSE;
		e(printf("Got unexpected line before greeting:  '%s'\n", token));
		g_free(token);
	}

	if (!is->cinfo) {
		ic = camel_imapx_command_new(is, "CAPABILITY", NULL, "CAPABILITY");
		imapx_command_run(is, ic);

		if (ic->error != NULL || ic->status->result != IMAPX_OK) {
			if (ic->error == NULL)
				g_set_error (
					error, CAMEL_ERROR,
					CAMEL_ERROR_GENERIC,
					"%s", ic->status->text);
			else {
				g_propagate_error (error, ic->error);
				ic->error = NULL;
			}

			camel_imapx_command_free(ic);
			return FALSE;
		}
		camel_imapx_command_free(ic);
	}

#ifdef CAMEL_HAVE_SSL
	if (ssl_mode == 2)
	{

		if (!(is->cinfo->capa & IMAPX_CAPABILITY_STARTTLS)) {
			g_set_error (
				&local_error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				_("Failed to connect to IMAP server %s in secure mode: %s"),
				is->url->host, _("STARTTLS not supported"));
			goto exit;
		}

		ic = camel_imapx_command_new (is, "STARTTLS", NULL, "STARTTLS");
		imapx_command_run (is, ic);

		if (ic->error != NULL || ic->status->result != IMAPX_OK) {
			if (ic->error == NULL)
				g_set_error (
					&local_error, CAMEL_ERROR,
					CAMEL_ERROR_GENERIC,
					"%s", ic->status->text);
			else {
				g_propagate_error (&local_error, ic->error);
				ic->error = NULL;
			}

			camel_imapx_command_free(ic);
			goto exit;
		}

		/* See if we got new capabilities in the STARTTLS response */
		imapx_free_capability(is->cinfo);
		is->cinfo = NULL;
		if (ic->status->condition == IMAPX_CAPABILITY) {
			is->cinfo = ic->status->u.cinfo;
			ic->status->u.cinfo = NULL;
			c(printf("got capability flags %08x\n", is->cinfo->capa));
		}

		camel_imapx_command_free(ic);

		if (camel_tcp_stream_ssl_enable_ssl (CAMEL_TCP_STREAM_SSL (tcp_stream)) == -1) {
			g_set_error (
				&local_error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				_("Failed to connect to IMAP server %s in secure mode: %s"),
				is->url->host, _("SSL negotiations failed"));
			goto exit;
		}
		/* Get new capabilities if they weren't already given */
		if (!is->cinfo) {
			ic = camel_imapx_command_new(is, "CAPABILITY", NULL, "CAPABILITY");
			if (!imapx_command_run (is, ic)) {
				g_propagate_error (&local_error, ic->error);
				ic->error = NULL;
				camel_imapx_command_free (ic);
				goto exit;
			}

			camel_imapx_command_free(ic);
		}
	}
#endif

exit:
	if (local_error != NULL) {
		e(printf("Unable to connect %d %s \n", local_error->code, local_error->message));
		g_propagate_error (error, local_error);
		local_error = NULL;
		g_object_unref (is->stream);
		is->stream = NULL;

		if (is->cinfo) {
			imapx_free_capability(is->cinfo);
			is->cinfo = NULL;
		}

		return FALSE;
	}

	return TRUE;
}

static gboolean
imapx_reconnect (CamelIMAPXServer *is, GError **error)
{
	CamelSasl *sasl;
	CamelIMAPXCommand *ic;
	gchar *errbuf = NULL;
	CamelService *service = (CamelService *) is->store;
	const gchar *auth_domain = NULL;
	gboolean authenticated = FALSE;
	CamelServiceAuthType *authtype = NULL;
	guint32 prompt_flags = CAMEL_SESSION_PASSWORD_SECRET;

	while (!authenticated) {
		if (errbuf) {
			/* We need to un-cache the password before prompting again */
			prompt_flags |= CAMEL_SESSION_PASSWORD_REPROMPT;
			g_free (service->url->passwd);
			service->url->passwd = NULL;
		}

		if (!imapx_connect_to_server (is, error))
			goto exception;

		if (is->state == IMAPX_AUTHENTICATED)
			goto preauthed;

		if (!authtype && service->url->authmech) {
			if (!g_hash_table_lookup (is->cinfo->auth_types, service->url->authmech)) {
				g_set_error (
					error, CAMEL_SERVICE_ERROR,
					CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
					_("IMAP server %s does not support requested "
					  "authentication type %s"),
					service->url->host,
					service->url->authmech);
				goto exception;
			}

			authtype = camel_sasl_authtype (service->url->authmech);
			if (!authtype) {
				g_set_error (
					error, CAMEL_SERVICE_ERROR,
					CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
					_("No support for authentication type %s"),
					service->url->authmech);
				goto exception;
			}
		}

		if (service->url->passwd == NULL && (!authtype || authtype->need_password)) {
			gchar *base_prompt;
			gchar *full_prompt;

			base_prompt = camel_session_build_password_prompt (
					"IMAP", service->url->user, service->url->host);

			if (errbuf != NULL)
				full_prompt = g_strconcat (errbuf, base_prompt, NULL);
			else
				full_prompt = g_strdup (base_prompt);

			auth_domain = camel_url_get_param (service->url, "auth-domain");
			service->url->passwd = camel_session_get_password(is->session, (CamelService *)is->store,
					auth_domain,
					full_prompt, "password", prompt_flags, error);

			g_free (base_prompt);
			g_free (full_prompt);
			g_free (errbuf);
			errbuf = NULL;

			if (!service->url->passwd) {
				g_set_error (
					error, CAMEL_SERVICE_ERROR,
					CAMEL_SERVICE_ERROR_NEED_PASSWORD,
					_("Need password for authentication"));
				goto exception;
			}
		}
		if (authtype && (sasl = camel_sasl_new ("imap", authtype->authproto, service))) {
			ic = camel_imapx_command_new (is, "AUTHENTICATE", NULL, "AUTHENTICATE %A", sasl);
			g_object_unref (sasl);
		} else {
			ic = camel_imapx_command_new(is, "LOGIN", NULL, "LOGIN %s %s", service->url->user, service->url->passwd);
		}

		imapx_command_run (is, ic);

		if (ic->error == NULL && ic->status->result == IMAPX_OK) {
			/* Forget old capabilities after login */
			if (is->cinfo) {
				imapx_free_capability(is->cinfo);
				is->cinfo = NULL;
			}

			if (ic->status->condition == IMAPX_CAPABILITY) {
				is->cinfo = ic->status->u.cinfo;
				ic->status->u.cinfo = NULL;
				c(printf("got capability flags %08x\n", is->cinfo->capa));
			}

			authenticated = TRUE;
		} else {
			/* If exception is set, it might be mostly due to cancellation and we would get an
			   io error, else re-prompt. If authentication fails for other reasons ic->status would be
			    set with the error message */
			if (ic->error != NULL) {
				g_propagate_error (error, ic->error);
				ic->error = NULL;
				camel_imapx_command_free(ic);
				goto exception;
			}

			errbuf = g_markup_printf_escaped (
					_("Unable to authenticate to IMAP server.\n%s\n\n"),
					 ic->status->text);
		}

		camel_imapx_command_free(ic);
	}

	/* After login we re-capa unless the server already told us */
	if (!is->cinfo) {
		ic = camel_imapx_command_new(is, "CAPABILITY", NULL, "CAPABILITY");
		if (!imapx_command_run (is, ic)) {
			g_propagate_error (error, ic->error);
			ic->error = NULL;
			camel_imapx_command_free(ic);
			goto exception;
		}

		camel_imapx_command_free (ic);
	}

	is->state = IMAPX_AUTHENTICATED;

 preauthed:
	if (((CamelIMAPXStore *)is->store)->rec_options & IMAPX_USE_IDLE)
		is->use_idle = TRUE;
	else
		is->use_idle = FALSE;

	if (imapx_idle_supported (is))
		imapx_init_idle (is);

	/* Fetch namespaces */
	if (is->cinfo->capa & IMAPX_CAPABILITY_NAMESPACE) {
		ic = camel_imapx_command_new (is, "NAMESPACE", NULL, "NAMESPACE");
		if (!imapx_command_run (is, ic)) {
			g_propagate_error (error, ic->error);
			ic->error = NULL;
			camel_imapx_command_free (ic);
			goto exception;
		}

		camel_imapx_command_free (ic);
	}

	if (((CamelIMAPXStore *)is->store)->rec_options & IMAPX_USE_QRESYNC &&
	    is->cinfo->capa & IMAPX_CAPABILITY_QRESYNC) {
		ic = camel_imapx_command_new (is, "ENABLE", NULL, "ENABLE CONDSTORE QRESYNC");
		if (!imapx_command_run (is, ic)) {
			g_propagate_error (error, ic->error);
			ic->error = NULL;
			camel_imapx_command_free (ic);
			goto exception;
		}

		camel_imapx_command_free (ic);

		is->use_qresync = TRUE;
	} else
		is->use_qresync = FALSE;

	if (((CamelIMAPXStore *) is->store)->summary->namespaces == NULL) {
		CamelIMAPXNamespaceList *nsl = NULL;
		CamelIMAPXStoreNamespace *ns = NULL;
		CamelIMAPXStore *imapx_store = (CamelIMAPXStore *) is->store;

		/* set a default namespace */
		nsl = g_malloc0(sizeof(CamelIMAPXNamespaceList));
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
	return TRUE;

exception:

	imapx_disconnect (is);

	if (is->cinfo) {
		imapx_free_capability(is->cinfo);
		is->cinfo = NULL;
	}

	return FALSE;
}

/* ********************************************************************** */

static void
imapx_command_fetch_message_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;
	const gchar *full_name;
	gboolean failed = FALSE;

	/* We either have more to fetch (partial mode?), we are complete,
	   or we failed.  Failure is handled in the fetch code, so
	   we just return the job, or keep it alive with more requests */

	job->commands--;

	full_name = camel_folder_get_full_name (job->folder);

	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		failed = TRUE;
		job->u.get_message.body_len = -1;
	} else  if (job->u.get_message.use_multi_fetch) {
		gsize really_fetched = CAMEL_SEEKABLE_STREAM(job->u.get_message.stream)->position;
		/* Don't automatically stop when we reach the reported message
		   size -- some crappy servers (like Microsoft Exchange) have
		   a tendency to lie about it. Keep going (one request at a
		   time) until the data actually stop coming. */
		if (job->u.get_message.fetch_offset < job->u.get_message.size ||
		    job->u.get_message.fetch_offset == really_fetched) {
			camel_imapx_command_free (ic);
			if (job->op)
				camel_operation_progress (job->op, (job->u.get_message.fetch_offset *100)/job->u.get_message.size);

			ic = camel_imapx_command_new(is, "FETCH", job->folder,
					"UID FETCH %t (BODY.PEEK[]", job->u.get_message.uid);
			camel_imapx_command_add(ic, "<%u.%u>", job->u.get_message.fetch_offset, MULTI_SIZE);
			camel_imapx_command_add(ic, ")");
			ic->complete = imapx_command_fetch_message_done;
			ic->job = job;
			ic->pri = job->pri - 1;
			job->u.get_message.fetch_offset += MULTI_SIZE;
			job->commands++;
			imapx_command_queue(is, ic);
			return;
		}
	}

	if (job->commands == 0) {
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
		CamelStream *stream = job->u.get_message.stream;

		/* return the exception from last command */
		if (failed) {
			if (ic->error == NULL)
				g_set_error (
					&job->error, CAMEL_IMAPX_ERROR, 1,
					"Error fetching message: %s", ic->status->text);
			else {
				g_propagate_error (&job->error, ic->error);
				ic->error = NULL;
			}
			g_object_unref (stream);
			job->u.get_message.stream = NULL;
		} else {
			CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;

			if (stream) {
				gchar *tmp = camel_data_cache_get_filename (ifolder->cache, "tmp", job->u.get_message.uid, NULL);

				if (camel_stream_flush (stream, &job->error) == 0 && camel_stream_close (stream, &job->error) == 0) {
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
				job->u.get_message.stream = camel_data_cache_get (ifolder->cache, "cur", job->u.get_message.uid, NULL);
			}
		}

		camel_data_cache_remove (ifolder->cache, "tmp", job->u.get_message.uid, NULL);
		imapx_job_done (is, job);
	}

	camel_imapx_command_free (ic);
}

static void
imapx_job_get_message_start (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	gint i;

	if (job->u.get_message.use_multi_fetch) {
		for (i=0; i < 3 && job->u.get_message.fetch_offset < job->u.get_message.size;i++) {
			ic = camel_imapx_command_new(is, "FETCH", job->folder,
					"UID FETCH %t (BODY.PEEK[]", job->u.get_message.uid);
			camel_imapx_command_add(ic, "<%u.%u>", job->u.get_message.fetch_offset, MULTI_SIZE);
			camel_imapx_command_add(ic, ")");
			ic->complete = imapx_command_fetch_message_done;
			ic->job = job;
			ic->pri = job->pri;
			job->u.get_message.fetch_offset += MULTI_SIZE;
			job->commands++;
			imapx_command_queue(is, ic);
		}
	} else {
		ic = camel_imapx_command_new(is, "FETCH", job->folder,
				"UID FETCH %t (BODY.PEEK[])", job->u.get_message.uid);
		ic->complete = imapx_command_fetch_message_done;
		ic->job = job;
		ic->pri = job->pri;
		job->commands++;
		imapx_command_queue(is, ic);
	}
}

/* ********************************************************************** */

static void
imapx_command_copy_messages_step_start (CamelIMAPXServer *is, CamelIMAPXJob *job, gint index)
{
	CamelIMAPXCommand *ic;
	GPtrArray *uids = job->u.copy_messages.uids;
	gint i = index;

	ic = camel_imapx_command_new (is, "COPY", job->folder, "UID COPY ");
	ic->complete = imapx_command_copy_messages_step_done;
	ic->job = job;
	ic->pri = job->pri;
	job->u.copy_messages.last_index = i;

	for (;i < uids->len; i++) {
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
imapx_command_copy_messages_step_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;
	gint i = job->u.copy_messages.index;
	GPtrArray *uids = job->u.copy_messages.uids;

	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&job->error, CAMEL_IMAPX_ERROR, 1,
				"Error copying messages");
		else {
			g_propagate_error (&job->error, ic->error);
			ic->error = NULL;
		}

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
			guint32 uid = GPOINTER_TO_UINT(g_ptr_array_index (ic->status->u.copyuid.copied_uids, i));
			gchar *str = g_strdup_printf ("%d",uid);
			CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->u.copy_messages.dest;

			g_hash_table_insert (ifolder->ignore_recent, str, GINT_TO_POINTER (1));
		}

	}

	if (i < uids->len) {
		camel_imapx_command_free (ic);
		imapx_command_copy_messages_step_start (is, job, i);
		return;
	}

cleanup:
	g_object_unref (job->u.copy_messages.dest);
	g_object_unref (job->folder);

	imapx_job_done (is, job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_copy_messages_start (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	if (!imapx_server_sync_changes (is, job->folder, job->pri, &job->error))
		imapx_job_done (is, job);

	g_ptr_array_sort (job->u.copy_messages.uids, (GCompareFunc) imapx_uids_array_cmp);
	imapx_uidset_init(&job->u.copy_messages.uidset, 0, MAX_COMMAND_LEN);
	imapx_command_copy_messages_step_start (is, job, 0);
}

/* ********************************************************************** */

static void
imapx_command_append_message_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
	CamelMessageInfo *mi;
	gchar *cur, *old_uid;

	/* Append done.  If we the server supports UIDPLUS we will get an APPENDUID response
	   with the new uid.  This lets us move the message we have directly to the cache
	   and also create a correctly numbered MessageInfo, without losing any information.
	   Otherwise we have to wait for the server to less us know it was appended. */

	mi = camel_message_info_clone (job->u.append_message.info);
	old_uid = g_strdup (job->u.append_message.info->uid);

	if (ic->error == NULL && ic->status->result == IMAPX_OK) {
		if (ic->status->condition == IMAPX_APPENDUID) {
			c(printf("Got appenduid %d %d\n", (gint)ic->status->u.appenduid.uidvalidity, (gint)ic->status->u.appenduid.uid));
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
									      ((CamelMessageInfoBase *)job->u.append_message.info)->flags,
									      ((CamelMessageInfoBase *)job->u.append_message.info)->user_flags,
									      job->folder);
				changes = camel_folder_change_info_new ();
				camel_folder_change_info_add_uid (changes, mi->uid);
				camel_folder_changed (job->folder, changes);
				camel_folder_change_info_free (changes);

				g_free(cur);
			} else {
				g_message ("but uidvalidity changed \n");
			}
		}
	} else {
		if (ic->error == NULL)
			g_set_error (
				&job->error, CAMEL_IMAPX_ERROR, 1,
				"Error appending message: %s", ic->status->text);
		else {
			g_propagate_error (&job->error, ic->error);
			ic->error = NULL;
		}
	}

	camel_data_cache_remove (ifolder->cache, "new", old_uid, NULL);
	g_free (old_uid);
	camel_message_info_free(job->u.append_message.info);
	g_free(job->u.append_message.path);
	g_object_unref (job->folder);

	imapx_job_done (is, job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_append_message_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	/* TODO: we could supply the original append date from the file timestamp */
	ic = camel_imapx_command_new(is, "APPEND", NULL,
				     "APPEND %f %F %P",
				     job->folder,
				     ((CamelMessageInfoBase *)job->u.append_message.info)->flags,
				     ((CamelMessageInfoBase *)job->u.append_message.info)->user_flags,
				     job->u.append_message.path);
	ic->complete = imapx_command_append_message_done;
	ic->job = job;
	ic->pri = job->pri;
	job->commands++;
	imapx_command_queue(is, ic);
}

/* ********************************************************************** */

static gint
imapx_refresh_info_uid_cmp (gconstpointer ap, gconstpointer bp)
{
	guint av, bv;

	av = g_ascii_strtoull ((const gchar *)ap, NULL, 10);
	bv = g_ascii_strtoull ((const gchar *)bp, NULL, 10);

	if (av<bv)
		return -1;
	else if (av>bv)
		return 1;
	else
		return 0;
}

static gint
imapx_uids_array_cmp (gconstpointer ap, gconstpointer bp)
{
	const gchar **a = (const gchar **) ap;
        const gchar **b = (const gchar **) bp;

	return imapx_refresh_info_uid_cmp (*a, *b);
}

static gint
imapx_refresh_info_cmp (gconstpointer ap, gconstpointer bp)
{
	const struct _refresh_info *a = ap;
	const struct _refresh_info *b = bp;

	return imapx_refresh_info_uid_cmp (a->uid, b->uid);
}

/* skips over non-server uids (pending appends) */
static guint
imapx_index_next (GPtrArray *uids, CamelFolderSummary *s, guint index)
{

	while (index < uids->len) {
		CamelMessageInfo *info;

		index++;
		if (index >= uids->len)
			break;

		info = camel_folder_summary_uid (s, g_ptr_array_index (uids, index));
		if (!info)
			continue;

		if (info && (strchr(camel_message_info_uid(info), '-') != NULL)) {
			camel_message_info_free (info);
			e(printf("Ignoring offline uid '%s'\n", camel_message_info_uid(info)));
		} else {
			camel_message_info_free (info);
			break;
		}
	}

	return index;
}

static void
imapx_command_step_fetch_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *)ic->job->folder;
	CamelIMAPXSummary *isum = (CamelIMAPXSummary *)ic->job->folder->summary;
	CamelIMAPXJob *job = ic->job;
	gint i = job->u.refresh_info.index;
	GArray *infos = job->u.refresh_info.infos;

	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&job->error, CAMEL_IMAPX_ERROR, 1,
				"Error fetching message headers");
		else {
			g_propagate_error (&job->error, ic->error);
			ic->error = NULL;
		}

		goto cleanup;
	}

	if (camel_folder_change_info_changed(job->u.refresh_info.changes)) {
		imapx_update_store_summary (job->folder);
		camel_folder_summary_save_to_db (job->folder->summary, NULL);
		camel_folder_changed (job->folder, job->u.refresh_info.changes);
	}

	camel_folder_change_info_clear(job->u.refresh_info.changes);

	if (i<infos->len) {
		camel_imapx_command_free (ic);

		ic = camel_imapx_command_new(is, "FETCH", job->folder, "UID FETCH ");
		ic->complete = imapx_command_step_fetch_done;
		ic->job = job;
		ic->pri = job->pri - 1;
		job->u.refresh_info.last_index = i;

		for (;i<infos->len;i++) {
			gint res;
			struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

			if (!r->exists) {
				res = imapx_uidset_add(&job->u.refresh_info.uidset, ic, r->uid);
				if (res == 1) {
					camel_imapx_command_add(ic, " (RFC822.SIZE RFC822.HEADER)");
					job->u.refresh_info.index = i;
					imapx_command_queue(is, ic);
					return;
				}
			}
		}

		job->u.refresh_info.index = i;
		if (imapx_uidset_done(&job->u.refresh_info.uidset, ic)) {
			camel_imapx_command_add(ic, " (RFC822.SIZE RFC822.HEADER)");
			imapx_command_queue(is, ic);
			return;
		}
	}

	if (camel_folder_summary_count(job->folder->summary)) {
		gchar *uid = camel_folder_summary_uid_from_index (job->folder->summary,
						  camel_folder_summary_count(job->folder->summary) - 1);
		unsigned long long uidl = strtoull(uid, NULL, 10);
		g_free(uid);

		uidl++;

		if (uidl > ifolder->uidnext_on_server) {
			c(printf("Updating uidnext_on_server for '%s' to %lld\n",
				 camel_folder_get_full_name(job->folder), uidl));
			ifolder->uidnext_on_server = uidl;
		}
	}
	isum->uidnext = ifolder->uidnext_on_server;

 cleanup:
	for (i=0;i<infos->len;i++) {
		struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

		camel_flag_list_free (&r->server_user_flags);
		g_free(r->uid);
	}
	g_array_free(job->u.refresh_info.infos, TRUE);
	if (job->type == IMAPX_JOB_FETCH_NEW_MESSAGES)
		camel_folder_change_info_free (job->u.refresh_info.changes);

	imapx_job_done (is, job);
	camel_imapx_command_free (ic);
}

static gint
imapx_uid_cmp(gconstpointer ap, gconstpointer bp, gpointer data)
{
	const gchar *a = ap, *b = bp;
	gchar *ae, *be;
	unsigned long av, bv;

	av = strtoul(a, &ae, 10);
	bv = strtoul(b, &be, 10);

	if (av < bv)
		return -1;
	else if (av > bv)
		return 1;

	if (*ae == '-')
		ae++;
	if (*be == '-')
		be++;

	return strcmp(ae, be);
}

static void
imapx_job_scan_changes_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;
	gint i;
	GArray *infos = job->u.refresh_info.infos;

	if (ic->error == NULL && ic->status->result == IMAPX_OK) {
		GCompareDataFunc uid_cmp = imapx_uid_cmp;
		CamelMessageInfo *s_minfo = NULL;
		CamelIMAPXMessageInfo *info;
		CamelFolderSummary *s = job->folder->summary;
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *)job->folder;
		GSList *removed = NULL, *l;
		gboolean fetch_new = FALSE;
		gint i;
		guint j = 0;
		GPtrArray *uids;

		/* Actually we wanted to do this after the SELECT but before the
		   FETCH command was issued. But this should suffice. */
		((CamelIMAPXSummary *)s)->uidnext = ifolder->uidnext_on_server;
		((CamelIMAPXSummary *)s)->modseq = ifolder->modseq_on_server;

		/* Here we do the typical sort/iterate/merge loop.
		   If the server flags dont match what we had, we modify our
		   flags to pick up what the server now has - but we merge
		   not overwrite */

		/* FIXME: We also have to check the offline directory for
		   anything missing in our summary, and also queue up jobs
		   for all outstanding messages to be uploaded */

		/* obtain a copy to be thread safe */
		uids = camel_folder_summary_array (s);

		qsort(infos->data, infos->len, sizeof(struct _refresh_info), imapx_refresh_info_cmp);
		g_ptr_array_sort(uids, (GCompareFunc) imapx_uids_array_cmp);

		if (uids->len)
			s_minfo = camel_folder_summary_uid (s, g_ptr_array_index (uids, 0));

		for (i=0; i<infos->len; i++) {
			struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

			while (s_minfo && uid_cmp (camel_message_info_uid(s_minfo), r->uid, s) < 0) {
				const gchar *uid = camel_message_info_uid (s_minfo);

				camel_folder_change_info_remove_uid (job->u.refresh_info.changes, uid);
				removed = g_slist_prepend (removed, (gpointer ) g_strdup (uid));
				camel_message_info_free (s_minfo);
				s_minfo = NULL;

				j = imapx_index_next (uids, s, j);
				if (j < uids->len)
					s_minfo = camel_folder_summary_uid (s, g_ptr_array_index (uids, j));
			}

			info = NULL;
			if (s_minfo && uid_cmp(s_minfo->uid, r->uid, s) == 0) {
				info = (CamelIMAPXMessageInfo *)s_minfo;

				if (imapx_update_message_info_flags ((CamelMessageInfo *) info, r->server_flags, r->server_user_flags, job->folder, FALSE))
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
				s_minfo = camel_folder_summary_uid (s, g_ptr_array_index (uids, j));
		}

		if (s_minfo)
			camel_message_info_free (s_minfo);

		while (j < uids->len) {
			s_minfo = camel_folder_summary_uid (s, g_ptr_array_index (uids, j));

			if (!s_minfo) {
				j++;
				continue;
			}

			e(printf("Message %s vanished\n", s_minfo->uid));
			removed = g_slist_prepend (removed, (gpointer) g_strdup (s_minfo->uid));
			camel_message_info_free (s_minfo);
			j++;
		}

		for (l = removed; l != NULL; l = g_slist_next (l)) {
			gchar *uid = (gchar *) l->data;
			CamelMessageInfo *mi;

			mi = camel_folder_summary_uid (job->folder->summary, uid);
			if (mi) {
				imapx_update_summary_for_removed_message (mi, job->folder, FALSE);
				camel_message_info_free (mi);
			}

			camel_folder_change_info_remove_uid (job->u.refresh_info.changes, uid);
			camel_folder_summary_remove_uid_fast (s, uid);
		}

		if (removed) {
			const gchar *full_name;

			full_name = camel_folder_get_full_name (s->folder);
			camel_db_delete_uids (is->store->cdb_w, full_name, removed, NULL);
			g_slist_foreach (removed, (GFunc) g_free, NULL);
			g_slist_free (removed);
		}

		imapx_update_store_summary (job->folder);

		if (camel_folder_change_info_changed(job->u.refresh_info.changes))
			camel_folder_changed (job->folder, job->u.refresh_info.changes);
		camel_folder_change_info_clear(job->u.refresh_info.changes);

		camel_folder_free_uids (job->folder, uids);

		/* If we have any new messages, download their headers, but only a few (100?) at a time */
		if (fetch_new) {
			camel_operation_start (
				job->op, _("Fetching summary information for new messages in %s"),
				camel_folder_get_name (job->folder));
			imapx_uidset_init(&job->u.refresh_info.uidset, BATCH_FETCH_COUNT, 0);
			/* These are new messages which arrived since we last knew the unseen count;
			   update it as they arrive. */
			job->u.refresh_info.update_unseen = TRUE;
			imapx_command_step_fetch_done(is, ic);
			return;
		}
	} else {
		if (ic->error == NULL)
			g_set_error (
				&job->error, CAMEL_IMAPX_ERROR, 1,
				"Error retriving message: %s", ic->status->text);
		else {
			g_propagate_error (&job->error, ic->error);
			ic->error = NULL;
		}
	}

	for (i=0;i<infos->len;i++) {
		struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

		camel_flag_list_free (&r->server_user_flags);
		g_free(r->uid);
	}

	/* There's no sane way to get the server-side unseen count on the
	   select mailbox. So just work it out from the flags */
	((CamelIMAPXFolder *)job->folder)->unread_on_server = job->folder->summary->unread_count;

	g_array_free(job->u.refresh_info.infos, TRUE);
	imapx_job_done (is, job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_scan_changes_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	camel_operation_start (
		job->op, _("Scanning for changed messages in %s"),
		camel_folder_get_name (job->folder));

	ic = camel_imapx_command_new (is, "FETCH", job->folder,
				     "UID FETCH 1:* (UID FLAGS)");
	ic->job = job;
	ic->complete = imapx_job_scan_changes_done;
	ic->pri = job->pri;
	job->u.refresh_info.infos = g_array_new (0, 0, sizeof(struct _refresh_info));
	imapx_command_queue (is, ic);
}

static void
imapx_command_fetch_new_messages_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXSummary *isum = (CamelIMAPXSummary *)ic->job->folder->summary;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *)ic->job->folder;

	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&ic->job->error, CAMEL_IMAPX_ERROR, 1,
				"Error fetching new messages: %s", ic->status->text);
		else {
			g_propagate_error (&ic->job->error, ic->error);
			ic->error = NULL;
		}
		goto exception;
	}

	if (camel_folder_change_info_changed(ic->job->u.refresh_info.changes)) {
		imapx_update_store_summary (ic->job->folder);
		camel_folder_summary_save_to_db (ic->job->folder->summary, NULL);
		camel_folder_changed (ic->job->folder, ic->job->u.refresh_info.changes);
		camel_folder_change_info_clear(ic->job->u.refresh_info.changes);
	}

	if (camel_folder_summary_count(ic->job->folder->summary)) {
		gchar *uid = camel_folder_summary_uid_from_index (ic->job->folder->summary,
					  camel_folder_summary_count(ic->job->folder->summary) - 1);
		unsigned long long uidl = strtoull(uid, NULL, 10);
		g_free(uid);

		uidl++;

		if (uidl > ifolder->uidnext_on_server) {
			c(printf("Updating uidnext_on_server for '%s' to %lld\n",
				 camel_folder_get_full_name(ic->job->folder), uidl));
			ifolder->uidnext_on_server = uidl;
		}
	}

	isum->uidnext = ifolder->uidnext_on_server;

exception:
	camel_folder_change_info_free (ic->job->u.refresh_info.changes);

	if (ic->job->op)
		camel_operation_unref (ic->job->op);

	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_fetch_new_messages_start (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	CamelFolder *folder = job->folder;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	guint32 total, diff;
	gchar *uid = NULL;

	total = camel_folder_summary_count (folder->summary);
	diff = ifolder->exists_on_server - total;

	if (total > 0) {
		unsigned long long uidl;
		uid = camel_folder_summary_uid_from_index (folder->summary, total - 1);
		uidl = strtoull(uid, NULL, 10);
		g_free(uid);
		uid = g_strdup_printf("%lld", uidl+1);
	} else
		uid = g_strdup ("1");

	camel_operation_start (
		job->op, _("Fetching summary information for new messages in %s"),
		camel_folder_get_name (folder));

	if (diff > BATCH_FETCH_COUNT) {
		ic = camel_imapx_command_new (is, "FETCH", job->folder,
				     "UID FETCH %s:* (UID FLAGS)", uid);
		imapx_uidset_init(&job->u.refresh_info.uidset, BATCH_FETCH_COUNT, 0);
		job->u.refresh_info.infos = g_array_new (0, 0, sizeof(struct _refresh_info));
		ic->pri = job->pri;
		ic->complete = imapx_command_step_fetch_done;
	} else {
		ic = camel_imapx_command_new (is, "FETCH", job->folder,
					"UID FETCH %s:* (RFC822.SIZE RFC822.HEADER FLAGS)", uid);
		ic->pri = job->pri;
		ic->complete = imapx_command_fetch_new_messages_done;
	}

	g_free (uid);
	ic->job = job;
	imapx_command_queue (is, ic);
}

static void
imapx_job_refresh_info_start (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	guint32 total;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;
	CamelIMAPXSummary *isum = (CamelIMAPXSummary *)job->folder->summary;
	CamelFolder *folder = job->folder;
	const gchar *full_name;
	gboolean need_rescan = FALSE;
	gboolean is_selected = FALSE;
	gboolean can_qresync = FALSE;

	full_name = camel_folder_get_full_name (folder);

	/* Sync changes first, else unread count will not
	   match. Need to think about better ways for this */
	if (!imapx_server_sync_changes (is, folder, job->pri, &job->error))
		goto done;

#if 0 /* There are issues with this still; continue with the buggy behaviour
	 where we issue STATUS on the current folder, for now...*/
	if (is->select_folder == folder)
		is_selected = TRUE;
#endif
	total = camel_folder_summary_count (folder->summary);

	/* We don't have valid unread count or modseq for currently-selected server
	   (unless we want to re-SELECT it). We fake unread count when fetching
	   message flags, but don't depend on modseq for the selected folder */
	if (total != ifolder->exists_on_server ||
	    isum->uidnext != ifolder->uidnext_on_server ||
	    folder->summary->unread_count != ifolder->unread_on_server ||
	    (!is_selected && isum->modseq != ifolder->modseq_on_server))
		need_rescan = TRUE;

	/* This is probably the first check of this folder after startup;
	   use STATUS to check whether the cached summary is valid, rather
	   than blindly updating. Only for servers which support CONDSTORE
	   though. */
	if ((isum->modseq && !ifolder->modseq_on_server))
		need_rescan = FALSE;

	/* If we don't think there's anything to do, poke it to check */
	if (!need_rescan) {
		CamelIMAPXCommand *ic;

		if (is_selected) {
			/* We may not issue STATUS on the current folder. Use SELECT or NOOP instead. */
			if (0 /* server needs SELECT not just NOOP*/) {
				if (imapx_idle_supported(is) && imapx_in_idle(is))
					if (!imapx_stop_idle(is, &job->error))
						goto done;
				/* This doesn't work -- this is an immediate command, not queued */
				if (!imapx_select(is, folder, TRUE, &job->error))
					goto done;
			} else {
				/* Or maybe just NOOP, unless we're in IDLE in which case do nothing */
				if (!imapx_idle_supported(is) || !imapx_in_idle(is)) {
					if (!camel_imapx_server_noop(is, folder, &job->error))
						goto done;
				}
			}
		} else {
			if (is->cinfo->capa & IMAPX_CAPABILITY_CONDSTORE)
				ic = camel_imapx_command_new (is, "STATUS", NULL, "STATUS %f (MESSAGES UNSEEN UIDVALIDITY UIDNEXT HIGHESTMODSEQ)", folder);
			else
				ic = camel_imapx_command_new (is, "STATUS", NULL, "STATUS %f (MESSAGES UNSEEN UIDVALIDITY UIDNEXT)", folder);

			ic->job = job;
			ic->pri = job->pri;

			imapx_command_run_sync (is, ic);

			if (ic->error != NULL || ic->status->result != IMAPX_OK) {
				if (ic->error == NULL)
					g_set_error (
						&job->error, CAMEL_IMAPX_ERROR, 1,
						"Error refreshing folder: %s", ic->status->text);
				else {
					g_propagate_error (&job->error, ic->error);
					ic->error = NULL;
				}

				camel_imapx_command_free (ic);
				goto done;
			}
			camel_imapx_command_free (ic);
		}

		/* Recalulate need_rescan */
		if (total != ifolder->exists_on_server ||
		    isum->uidnext != ifolder->uidnext_on_server ||
		    folder->summary->unread_count != ifolder->unread_on_server ||
		    (!is_selected && isum->modseq != ifolder->modseq_on_server))
			need_rescan = TRUE;

	}

	if (is->use_qresync && isum->modseq && ifolder->uidvalidity_on_server)
		can_qresync = TRUE;

	e(printf("folder %s is %sselected, total %u / %u, unread %u / %u, modseq %llu / %llu, uidnext %u / %u: will %srescan\n",
		 full_name, is_selected?"": "not ", total, ifolder->exists_on_server,
		 folder->summary->unread_count, ifolder->unread_on_server,
		 (unsigned long long)isum->modseq, (unsigned long long)ifolder->modseq_on_server,
		 isum->uidnext, ifolder->uidnext_on_server,
		 need_rescan?"":"not "));

	/* Fetch new messages first, so that they appear to the user ASAP */
	if (ifolder->exists_on_server > total ||
	    ifolder->uidnext_on_server > isum->uidnext)
	{
		if (!total)
			need_rescan = FALSE;

		if (!imapx_server_fetch_new_messages (is, folder, FALSE, FALSE, &job->error))
			goto done;

		/* If QRESYNC-capable we'll have got all flags changes in SELECT */
		if (can_qresync)
			goto qresync_done;
	}

	if (!need_rescan)
		goto done;

	if (can_qresync) {
		/* Actually we only want to select it; no need for the NOOP */
		camel_imapx_server_noop(is, folder, &job->error);
	qresync_done:
		isum->modseq = ifolder->modseq_on_server;
		total = camel_folder_summary_count(job->folder->summary);
		if (total != ifolder->exists_on_server ||
		    folder->summary->unread_count != ifolder->unread_on_server ||
		    (isum->modseq != ifolder->modseq_on_server)) {
			c(printf("Eep, after QRESYNC we're out of sync. total %u / %u, unread %u / %u, modseq %" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT "\n",
				 total, ifolder->exists_on_server,
				 folder->summary->unread_count, ifolder->unread_on_server,
				 isum->modseq, ifolder->modseq_on_server));
		} else {
			c(printf("OK, after QRESYNC we're still in sync. total %u / %u, unread %u / %u, modseq %" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT "\n",
				 total, ifolder->exists_on_server,
				 folder->summary->unread_count, ifolder->unread_on_server,
				 isum->modseq, ifolder->modseq_on_server));
			goto done;
		}
	}

	imapx_job_scan_changes_start (is, job);
	return;

done:
	imapx_job_done (is, job);
}

/* ********************************************************************** */

static void
imapx_command_expunge_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&ic->job->error, CAMEL_IMAPX_ERROR, 1,
				"Error expunging message: %s", ic->status->text);
		else {
			g_propagate_error (&ic->job->error, ic->error);
			ic->error = NULL;
		}
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
			GSList *removed = NULL;
			gint i;

			changes = camel_folder_change_info_new ();
			for (i = 0; i < uids->len; i++) {
				gchar *uid = uids->pdata[i];
				CamelMessageInfo *mi = camel_folder_summary_uid (folder->summary, uid);

				if (mi) {
					imapx_update_summary_for_removed_message (mi, folder, FALSE);
					camel_message_info_free (mi);
				}

				camel_folder_summary_remove_uid_fast (folder->summary, uid);
				camel_folder_change_info_remove_uid (changes, uids->pdata[i]);
				removed = g_slist_prepend (removed, (gpointer) uids->pdata[i]);
			}

			camel_db_delete_uids (parent_store->cdb_w, full_name, removed, NULL);
			camel_folder_summary_save_to_db (folder->summary, NULL);
			camel_folder_changed (folder, changes);
			camel_folder_change_info_free (changes);

			g_slist_free (removed);
			g_ptr_array_foreach (uids, (GFunc) camel_pstring_free, NULL);
			g_ptr_array_free (uids, TRUE);
		}
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_expunge_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	imapx_server_sync_changes (is, job->folder, job->pri, &job->error);

	/* TODO handle UIDPLUS capability */
	ic = camel_imapx_command_new(is, "EXPUNGE", job->folder, "EXPUNGE");
	ic->job = job;
	ic->pri = job->pri;
	ic->complete = imapx_command_expunge_done;
	imapx_command_queue(is, ic);
}

/* ********************************************************************** */

static void
imapx_command_list_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&ic->job->error, CAMEL_IMAPX_ERROR, 1,
				"Error fetching folders: %s", ic->status->text);
		else {
			g_propagate_error (&ic->job->error, ic->error);
			ic->error = NULL;
		}
	}

	e(printf ("==== list or lsub completed ==== \n"));
	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_list_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	ic = camel_imapx_command_new(is, "LIST", NULL, "%s \"\" %s",
				     (job->u.list.flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)?"LSUB":"LIST",
				     job->u.list.pattern);
	if (job->u.list.ext) {
		/* Hm, we need a way to add atoms _without_ quoting or using literals */
		camel_imapx_command_add(ic, " ");
		camel_imapx_command_add(ic, job->u.list.ext);
	}
	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_list_done;
	imapx_command_queue(is, ic);
}
/* ********************************************************************** */

static gchar *
imapx_encode_folder_name (CamelIMAPXStore *istore, const gchar *folder_name)
{
	gchar *fname, *encoded;

	fname = camel_imapx_store_summary_full_from_path(istore->summary, folder_name);
	if (fname) {
		encoded = camel_utf8_utf7(fname);
		g_free (fname);
	} else
		encoded = camel_utf8_utf7 (folder_name);

	return encoded;
}

static void
imapx_command_subscription_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&ic->job->error, CAMEL_IMAPX_ERROR, 1,
				"Error subscribing to folder : %s", ic->status->text);
		else {
			g_propagate_error (&ic->job->error, ic->error);
			ic->error = NULL;
		}
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_manage_subscription_start (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	const gchar *str = NULL;
	gchar *encoded_fname = NULL;

	if (job->u.manage_subscriptions.subscribe)
		str = "SUBSCRIBE";
	else
		str = "UNSUBSCRIBE";

	encoded_fname = imapx_encode_folder_name ((CamelIMAPXStore *) is->store, job->u.manage_subscriptions.folder_name);
	ic = camel_imapx_command_new (is, str, NULL, "%s %s", str, encoded_fname);

	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_subscription_done;
	imapx_command_queue(is, ic);

	g_free (encoded_fname);
}

/* ********************************************************************** */

static void
imapx_command_create_folder_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&ic->job->error, CAMEL_IMAPX_ERROR, 1,
				"Error creating to folder: %s", ic->status->text);
		else {
			g_propagate_error (&ic->job->error, ic->error);
			ic->error = NULL;
		}
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_create_folder_start (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	gchar *encoded_fname = NULL;

	encoded_fname = camel_utf8_utf7 (job->u.folder_name);
	ic = camel_imapx_command_new (is, "CREATE", NULL, "CREATE %s", encoded_fname);
	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_create_folder_done;
	imapx_command_queue(is, ic);

	g_free (encoded_fname);
}

/* ********************************************************************** */

static void
imapx_command_delete_folder_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&ic->job->error, CAMEL_IMAPX_ERROR, 1,
				"Error deleting to folder : %s", ic->status->text);
		else {
			g_propagate_error (&ic->job->error, ic->error);
			ic->error = NULL;
		}
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_delete_folder_start (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	gchar *encoded_fname = NULL;

	encoded_fname = imapx_encode_folder_name ((CamelIMAPXStore *) is->store, job->u.folder_name);

	job->folder = camel_store_get_folder(is->store, "INBOX", 0, &job->error);

	/* make sure to-be-deleted folder is not selected by selecting INBOX for this operation */
	ic = camel_imapx_command_new (is, "DELETE", job->folder, "DELETE %s", encoded_fname);
	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_delete_folder_done;
	imapx_command_queue(is, ic);

	g_free (encoded_fname);
}

/* ********************************************************************** */

static void
imapx_command_rename_folder_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&ic->job->error, CAMEL_IMAPX_ERROR, 1,
				"Error renaming to folder: %s", ic->status->text);
		else {
			g_propagate_error (&ic->job->error, ic->error);
			ic->error = NULL;
		}
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_rename_folder_start (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	gchar *en_ofname = NULL, *en_nfname = NULL;

	job->folder = camel_store_get_folder(is->store, "INBOX", 0, &job->error);

	en_ofname = imapx_encode_folder_name ((CamelIMAPXStore *) is->store, job->u.rename_folder.ofolder_name);
	en_nfname = imapx_encode_folder_name ((CamelIMAPXStore *) is->store, job->u.rename_folder.nfolder_name);

	ic = camel_imapx_command_new (is, "RENAME", job->folder, "RENAME %s %s", en_ofname, en_nfname);
	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_rename_folder_done;
	imapx_command_queue(is, ic);

	g_free (en_ofname);
	g_free (en_nfname);
}

/* ********************************************************************** */

static void
imapx_command_noop_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (ic->error == NULL)
			g_set_error (
				&ic->job->error, CAMEL_IMAPX_ERROR, 1,
				"Error performing NOOP: %s", ic->status->text);
		else {
			g_propagate_error (&ic->job->error, ic->error);
			ic->error = NULL;
		}
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_noop_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	ic = camel_imapx_command_new (is, "NOOP", job->folder, "NOOP");

	ic->job = job;
	ic->complete = imapx_command_noop_done;
	if (job->folder)
		ic->pri = IMAPX_PRIORITY_REFRESH_INFO;
	else
		ic->pri = IMAPX_PRIORITY_NOOP;
	imapx_command_queue(is, ic);
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
  flags 00101000
 sflags 01001000
 ^      01100000
~flags  11010111
&       01000000

&flags  00100000
*/

static void
imapx_command_sync_changes_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;
	CamelStore *parent_store;
	const gchar *full_name;
	gboolean failed = FALSE;

	job->commands--;

	full_name = camel_folder_get_full_name (job->folder);
	parent_store = camel_folder_get_parent_store (job->folder);

	/* If this worked, we should really just update the changes that we sucessfully
	   stored, so we dont have to worry about sending them again ...
	   But then we'd have to track which uid's we actually updated, so its easier
	   just to refresh all of the ones we got.

	   Not that ... given all the asynchronicity going on, we're guaranteed
	   that what we just set is actually what is on the server now .. but
	   if it isn't, i guess we'll fix up next refresh */

	if (ic->error != NULL || ic->status->result != IMAPX_OK) {
		if (!job->error) {
			if (ic->error == NULL)
				g_set_error (
					&job->error, CAMEL_IMAPX_ERROR, 1,
					"Error syncing changes: %s", ic->status->text);
			else {
				g_propagate_error (&job->error, ic->error);
				ic->error = NULL;
			}
		} else if (ic->error) {
			g_clear_error (&ic->error);
		}

		failed = TRUE;
	}

	/* lock cache ? */
	if (!failed)
	{
		gint i;

		for (i=0;i<job->u.sync_changes.changed_uids->len;i++) {
			CamelIMAPXMessageInfo *xinfo = (CamelIMAPXMessageInfo *) camel_folder_summary_uid (job->folder->summary,
					job->u.sync_changes.changed_uids->pdata[i]);

			if (!xinfo)
				continue;

			xinfo->server_flags = ((CamelMessageInfoBase *)xinfo)->flags & CAMEL_IMAPX_SERVER_FLAGS;
			xinfo->info.flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
			xinfo->info.dirty = TRUE;
			camel_flag_list_copy (&xinfo->server_user_flags, &xinfo->info.user_flags);

			camel_folder_summary_touch (job->folder->summary);
			camel_message_info_free (xinfo);
		}
		/* Apply the changes to server-side unread count; it won't tell
		   us of these changes, of course. */
		((CamelIMAPXFolder *)job->folder)->unread_on_server += job->u.sync_changes.unread_change;
	}

	if (job->commands == 0) {
		if (job->folder->summary && (job->folder->summary->flags & CAMEL_SUMMARY_DIRTY) != 0) {
			CamelStoreInfo *si;

			/* ... and store's summary when folder's summary is dirty */
			si = camel_store_summary_path ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary, full_name);
			if (si) {
				if (si->total != job->folder->summary->saved_count || si->unread != job->folder->summary->unread_count) {
					si->total = job->folder->summary->saved_count;
					si->unread = job->folder->summary->unread_count;
					camel_store_summary_touch ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary);
				}

				camel_store_summary_info_free ((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary, si);
			}
		}

		camel_folder_summary_save_to_db (job->folder->summary, &job->error);
		camel_store_summary_save((CamelStoreSummary *)((CamelIMAPXStore *) parent_store)->summary);

		imapx_job_done (is, job);
	}
	camel_imapx_command_free (ic);
}

static void
imapx_job_sync_changes_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	guint32 i, j;
	struct _uidset_state ss;
	GPtrArray *uids = job->u.sync_changes.changed_uids;
	gint on;

	for (on=0;on<2;on++) {
		guint32 orset = on?job->u.sync_changes.on_set:job->u.sync_changes.off_set;
		GArray *user_set = on?job->u.sync_changes.on_user:job->u.sync_changes.off_user;

		for (j = 0; j < G_N_ELEMENTS (flags_table); j++) {
			guint32 flag = flags_table[j].flag;
			CamelIMAPXCommand *ic = NULL;

			if ((orset & flag) == 0)
				continue;

			c(printf("checking/storing %s flags '%s'\n", on?"on":"off", flags_table[j].name));
			imapx_uidset_init(&ss, 0, 100);
			for (i = 0; i < uids->len; i++) {
				CamelIMAPXMessageInfo *info = (CamelIMAPXMessageInfo *)camel_folder_summary_uid
										(job->folder->summary, uids->pdata[i]);
				guint32 flags;
				guint32 sflags;
				gint send;

				if (!info)
					continue;

				flags = ((CamelMessageInfoBase *)info)->flags & CAMEL_IMAPX_SERVER_FLAGS;
				sflags = info->server_flags & CAMEL_IMAPX_SERVER_FLAGS;
				send = 0;

				if ( (on && (((flags ^ sflags) & flags) & flag))
				     || (!on && (((flags ^ sflags) & ~flags) & flag))) {
					if (ic == NULL) {
						ic = camel_imapx_command_new(is, "STORE", job->folder, "UID STORE ");
						ic->complete = imapx_command_sync_changes_done;
						ic->job = job;
						ic->pri = job->pri;
					}
					send = imapx_uidset_add(&ss, ic, camel_message_info_uid(info));
				}
				if (send || (i == uids->len-1 && imapx_uidset_done(&ss, ic))) {
					job->commands++;
					camel_imapx_command_add(ic, " %tFLAGS.SILENT (%t)", on?"+":"-", flags_table[j].name);
					imapx_command_queue(is, ic);
					ic = NULL;
				}
				if (flag == CAMEL_MESSAGE_SEEN) {
					/* Remember how the server's unread count will change if this
					   command succeeds */
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

			for (j=0; j<user_set->len; j++) {
				struct _imapx_flag_change *c = &g_array_index(user_set, struct _imapx_flag_change, j);

				imapx_uidset_init(&ss, 0, 100);
				for (i=0; i < c->infos->len; i++) {
					CamelIMAPXMessageInfo *info = c->infos->pdata[i];

					if (ic == NULL) {
						ic = camel_imapx_command_new(is, "STORE", job->folder, "UID STORE ");
						ic->complete = imapx_command_sync_changes_done;
						ic->job = job;
						ic->pri = job->pri;
					}

					if (imapx_uidset_add(&ss, ic, camel_message_info_uid (info))
					    || (i == c->infos->len-1 && imapx_uidset_done (&ss, ic))) {
						job->commands++;
						camel_imapx_command_add(ic, " %tFLAGS.SILENT (%t)", on?"+":"-", c->name);
						imapx_command_queue(is, ic);
						ic = NULL;
					}
				}
			}
		}
	}

	/* Since this may start in another thread ... we need to
	   lock the commands count, ho hum */

	if (job->commands == 0) {
		imapx_job_done (is, job);
	}
}

/* we cancel all the commands and their jobs, so associated jobs will be notified */
static void
cancel_all_jobs (CamelIMAPXServer *is, GError *error)
{
	CamelIMAPXCommand **cw, *ic;
	gint i = 0;

	while (i < 2) {
		QUEUE_LOCK(is);
		if (i == 1)
			cw = (CamelIMAPXCommand **) &is->queue.head;
		else
			cw = (CamelIMAPXCommand **) &is->active.head;

		while ((*cw)->next) {
			ic = *cw;
			camel_dlist_remove ((CamelDListNode *)ic);
			QUEUE_UNLOCK(is);

			ic->error = g_error_copy (error);
			ic->complete (is, ic);

			QUEUE_LOCK(is);
		}
		QUEUE_UNLOCK(is);
		i++;
	}
}

/* ********************************************************************** */

static void
parse_contents (CamelIMAPXServer *is, GError **error)
{
	gint buffered = 0;
	GError *local_error = NULL;

	do {
		imapx_step (is, &local_error);

		buffered = camel_imapx_stream_buffered (is->stream);

	} while (buffered && local_error == NULL);

	if (local_error != NULL)
		g_propagate_error (error, local_error);
}

/*
   The main processing (reading) loop.

   Main area of locking required is command_queue
   and command_start_next, the 'literal' command,
   the jobs queue, the active queue, the queue
   queue. */
static gpointer
imapx_parser_thread (gpointer d)
{
	CamelIMAPXServer *is = d;
	CamelOperation *op;
	GError *local_error = NULL;

	op = camel_operation_new (NULL, NULL);
	camel_operation_register (op);
	is->op = op;

	while (local_error == NULL && is->stream) {
		camel_operation_uncancel (op);
#ifndef G_OS_WIN32
		if (is->is_process_stream)	{
			GPollFD fds[2] = { {0, 0, 0}, {0, 0, 0} };
			gint res;

			fds[0].fd = ((CamelStreamProcess *)is->stream->source)->sockfd;
			fds[0].events = G_IO_IN;
			fds[1].fd = camel_operation_cancel_fd (op);
			fds[1].events = G_IO_IN;
			res = g_poll(fds, 2, 1000*30);
			if (res == -1)
				g_usleep(1) /* ?? */ ;
			else if (res == 0)
				/* timed out */;
			else if (fds[0].revents & G_IO_IN) {
				parse_contents (is, &local_error);
			} else if (fds[1].revents & G_IO_IN)
				errno = EINTR;
		} else
#endif
		{
			PRPollDesc pollfds[2] = { };
			gint res;

			pollfds[0].fd = camel_tcp_stream_get_file_desc (CAMEL_TCP_STREAM (is->stream->source));
			pollfds[0].in_flags = PR_POLL_READ;
			pollfds[1].fd = camel_operation_cancel_prfd (op);
			pollfds[1].in_flags = PR_POLL_READ;

#include <prio.h>

			res = PR_Poll(pollfds, 2, PR_MillisecondsToInterval (30 * 1000));
			if (res == -1)
				g_usleep(1) /* ?? */ ;
			else if (res == 0) {
				/* timed out */
			} else if ((pollfds[0].out_flags & PR_POLL_READ)) {
				parse_contents (is, &local_error);
			} else if (pollfds[1].out_flags & PR_POLL_READ)
				errno = EINTR;
		}

		if (is->parser_quit) {
			g_set_error (
				&local_error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED,
				_("Cancelled"));
			break;
		}

		if (camel_operation_cancel_check (op)) {
			gint is_empty;

			QUEUE_LOCK (is);
			is_empty = camel_dlist_empty (&is->active);
			QUEUE_UNLOCK (is);

			if ((is_empty || (imapx_idle_supported (is) && imapx_in_idle (is))))
				camel_operation_uncancel (op);
			else
				g_set_error (
					&local_error, G_IO_ERROR,
					G_IO_ERROR_CANCELLED,
					_("Cancelled"));
		}
	}

	QUEUE_LOCK(is);
	is->state = IMAPX_SHUTDOWN;
	QUEUE_UNLOCK(is);

	cancel_all_jobs (is, local_error);

	g_clear_error (&local_error);

	QUEUE_LOCK (is);
	if (op) {
		camel_operation_unregister (op);
		camel_operation_unref (op);
	}
	is->op = NULL;
	QUEUE_UNLOCK (is);

	is->parser_quit = FALSE;

	g_signal_emit (is, signals[SHUTDOWN], 0);

	return NULL;
}

static void
imapx_server_finalize (GObject *object)
{
	CamelIMAPXServer *is = CAMEL_IMAPX_SERVER (object);

	camel_url_free (is->url);

	g_static_rec_mutex_free(&is->queue_lock);
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

	QUEUE_LOCK(server);
	server->state = IMAPX_SHUTDOWN;

	server->parser_quit = TRUE;
	if (server->op)
		camel_operation_cancel (server->op);
	QUEUE_UNLOCK(server);

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

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_server_parent_class)->dispose (object);
}

static void
camel_imapx_server_class_init(CamelIMAPXServerClass *class)
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
	camel_dlist_init(&is->queue);
	camel_dlist_init(&is->active);
	camel_dlist_init(&is->done);
	camel_dlist_init(&is->jobs);

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
camel_imapx_server_new(CamelStore *store, CamelURL *url)
{
	CamelIMAPXServer *is;

	is = g_object_new (CAMEL_TYPE_IMAPX_SERVER, NULL);
	is->session = g_object_ref (CAMEL_SERVICE (store)->session);
	is->store = store;
	is->url = camel_url_copy(url);

	return is;
}

static gboolean
imapx_disconnect (CamelIMAPXServer *is)
{
	gboolean ret = TRUE;

	g_static_rec_mutex_lock (&is->ostream_lock);

	if (is->stream) {
		if (camel_stream_close (is->stream->source, NULL) == -1)
			ret = FALSE;

		g_object_unref (CAMEL_OBJECT (is->stream));
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
		imapx_free_capability(is->cinfo);
		is->cinfo = NULL;
	}

	is->state = IMAPX_DISCONNECTED;

	g_static_rec_mutex_unlock (&is->ostream_lock);

	return ret;
}

/* Client commands */
gboolean
camel_imapx_server_connect (CamelIMAPXServer *is, GError **error)
{
	gboolean success;

	if (is->state == IMAPX_SHUTDOWN) {
		g_set_error (error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE, "Shutting down");
		return FALSE;
	}

	if (is->state >= IMAPX_INITIALISED)
		return TRUE;

	g_static_rec_mutex_lock (&is->ostream_lock);
	success = imapx_reconnect (is, error);
	g_static_rec_mutex_unlock (&is->ostream_lock);

	if (!success)
		return FALSE;

	is->parser_thread = g_thread_create((GThreadFunc) imapx_parser_thread, is, TRUE, NULL);
	return TRUE;
}

static CamelStream *
imapx_server_get_message (CamelIMAPXServer *is, CamelFolder *folder, CamelOperation *op, const gchar *uid, gint pri, GError **error)
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
		   we could just use the queue lock with a GCond, but instead
		   we have to use a GMutex. I miss the kernel waitqueues. */
		do {
			int this;

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

	mi = camel_folder_summary_uid (folder->summary, uid);
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

	job = g_malloc0(sizeof(*job));
	job->pri = pri;
	job->type = IMAPX_JOB_GET_MESSAGE;
	job->start = imapx_job_get_message_start;
	job->folder = folder;
	job->op = op;
	job->u.get_message.uid = (gchar *)uid;
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

	g_free(job);

	g_mutex_lock (is->fetch_mutex);
	is->fetch_count++;
	g_cond_broadcast (is->fetch_cond);
	g_mutex_unlock (is->fetch_mutex);

	return stream;
}

CamelStream *
camel_imapx_server_get_message(CamelIMAPXServer *is, CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelStream *stream;
	CamelOperation *op = camel_operation_registered ();

	stream = imapx_server_get_message(is, folder, op, uid, IMAPX_PRIORITY_GET_MESSAGE, error);
	if (op)
		camel_operation_unref (op);

	return stream;
}

gboolean
camel_imapx_server_sync_message (CamelIMAPXServer *is, CamelFolder *folder, const gchar *uid, GError **error)
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

	stream = imapx_server_get_message (is, folder, NULL, uid, IMAPX_PRIORITY_SYNC_MESSAGE, error);

	if (stream == NULL)
		return FALSE;

	g_object_unref (stream);

	return TRUE;
}

gboolean
camel_imapx_server_copy_message (CamelIMAPXServer *is, CamelFolder *source, CamelFolder *dest, GPtrArray *uids, gboolean delete_originals, GError **error)
{
	CamelIMAPXJob *job;

	job = g_malloc0(sizeof(*job));
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
camel_imapx_server_append_message(CamelIMAPXServer *is, CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *mi, GError **error)
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
	   directory, and let the summary know it's there.  Then we fire off a no-reply
	   job which will asynchronously upload the message at some point in the future,
	   and fix up the summary to match */

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
	canon = camel_mime_filter_canon_new(CAMEL_MIME_FILTER_CANON_CRLF);
	camel_stream_filter_add((CamelStreamFilter *)filter, canon);
	res = camel_data_wrapper_write_to_stream (
		(CamelDataWrapper *)message, filter, error);
	g_object_unref (canon);
	g_object_unref (filter);

	if (res == -1) {
		g_prefix_error (error, _("Cannot create spool file: "));
		camel_data_cache_remove (ifolder->cache, "new", uid, NULL);
		g_free (uid);
		return FALSE;
	}

	tmp = camel_data_cache_get_filename (ifolder->cache, "new", uid, NULL);
	info = camel_folder_summary_info_new_from_message((CamelFolderSummary *)folder->summary, message, NULL);
	info->uid = camel_pstring_strdup (uid);
	if (mi)
		((CamelMessageInfoBase *) info)->flags = ((CamelMessageInfoBase *) mi)->flags;
	g_free (uid);

	/* So, we actually just want to let the server loop that
	   messages need appending, i think.  This is so the same
	   mechanism is used for normal uploading as well as
	   offline re-syncing when we go back online */

	job = g_malloc0(sizeof(*job));
	job->pri = IMAPX_PRIORITY_APPEND_MESSAGE;
	job->type = IMAPX_JOB_APPEND_MESSAGE;
	job->start = imapx_job_append_message_start;
	job->folder = g_object_ref (folder);
	job->noreply = FALSE;
	job->u.append_message.info = info;
	job->u.append_message.path = tmp;

	success = imapx_submit_job (is, job, error);

	g_free (job);

	return success;
}

gboolean
camel_imapx_server_noop (CamelIMAPXServer *is, CamelFolder *folder, GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_NOOP;
	job->start = imapx_job_noop_start;
	job->folder = folder;
	job->pri = IMAPX_PRIORITY_NOOP;

	success = imapx_submit_job (is, job, error);

	g_free(job);

	return success;
}

gboolean
camel_imapx_server_refresh_info (CamelIMAPXServer *is, CamelFolder *folder, GError **error)
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

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_REFRESH_INFO;
	job->start = imapx_job_refresh_info_start;
	job->folder = folder;
	job->op = camel_operation_registered ();
	job->u.refresh_info.changes = camel_folder_change_info_new();
	job->pri = IMAPX_PRIORITY_REFRESH_INFO;

	if (g_ascii_strcasecmp(full_name, "INBOX") == 0)
		job->pri += 10;

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && imapx_run_job (is, job, error);

	if (success && camel_folder_change_info_changed(job->u.refresh_info.changes))
		camel_folder_changed (folder, job->u.refresh_info.changes);

	camel_folder_change_info_free(job->u.refresh_info.changes);

	if (job->op)
		camel_operation_unref (job->op);
	g_free(job);

	return success;
}

static void
imapx_sync_free_user(GArray *user_set)
{
	gint i;

	if (user_set == NULL)
		return;

	for (i=0;i<user_set->len;i++) {
		struct _imapx_flag_change *flag_change = &g_array_index (user_set, struct _imapx_flag_change, i);
		GPtrArray *infos = flag_change->infos;
		gint j;

		for (j = 0; j < infos->len; j++) {
			CamelMessageInfo *info = g_ptr_array_index (infos, j);
			camel_message_info_free (info);
		}

		g_ptr_array_free(infos, TRUE);
		g_free (flag_change->name);
	}
	g_array_free(user_set, TRUE);
}

static gboolean
imapx_server_sync_changes(CamelIMAPXServer *is, CamelFolder *folder, gint pri, GError **error)
{
	guint i, on_orset, off_orset;
	GPtrArray *uids;
	GArray *on_user = NULL, *off_user = NULL;
	CamelIMAPXMessageInfo *info;
	CamelIMAPXJob *job;
	gboolean registered;
	gboolean success = TRUE;

	/* We calculate two masks, a mask of all flags which have been
	   turned off and a mask of all flags which have been turned
	   on. If either of these aren't 0, then we have work to do,
	   and we fire off a job to do it.

	   User flags are a bit more tricky, we rely on the user
	   flags being sorted, and then we create a bunch of lists;
	   one for each flag being turned off, including each
	   info being turned off, and one for each flag being turned on.
	*/
	uids = camel_folder_summary_get_changed (folder->summary);

	if (uids->len == 0) {
		camel_folder_free_uids (folder, uids);
		return TRUE;
	}

	off_orset = on_orset = 0;
	for (i=0; i < uids->len; i++) {
		guint32 flags, sflags;
		CamelFlag *uflags, *suflags;
		guint j = 0;

		info = (CamelIMAPXMessageInfo *) camel_folder_summary_uid (folder->summary, uids->pdata[i]);

		if (!info)
			continue;

		if (!(info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			camel_message_info_free (info);
			continue;
		}

		flags = ((CamelMessageInfoBase *)info)->flags & CAMEL_IMAPX_SERVER_FLAGS;
		sflags = info->server_flags & CAMEL_IMAPX_SERVER_FLAGS;
		if (flags != sflags) {
			off_orset |= ( flags ^ sflags ) & ~flags;
			on_orset |= (flags ^ sflags) & flags;
		}

		uflags = ((CamelMessageInfoBase *)info)->user_flags;
		suflags = info->server_user_flags;
		while (uflags || suflags) {
			gint res;

			if (uflags) {
				if (suflags)
					res = strcmp(uflags->name, suflags->name);
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
						on_user = g_array_new(FALSE, FALSE, sizeof(struct _imapx_flag_change));
					user_set = on_user;
					user_flag = uflags;
					uflags = uflags->next;
				} else {
					if (off_user == NULL)
						off_user = g_array_new(FALSE, FALSE, sizeof(struct _imapx_flag_change));
					user_set = off_user;
					user_flag = suflags;
					suflags = suflags->next;
				}

				/* Could sort this and binary search */
				for (j = 0; j < user_set->len; j++) {
					change = &g_array_index(user_set, struct _imapx_flag_change, j);
					if (strcmp(change->name, user_flag->name) == 0)
						goto found;
				}
				add.name = g_strdup(user_flag->name);
				add.infos = g_ptr_array_new();
				g_array_append_val(user_set, add);
				change = &add;
			found:
				camel_message_info_ref (info);
				g_ptr_array_add(change->infos, info);
			}
		}
		camel_message_info_free (info);
	}

	if ((on_orset|off_orset) == 0 && on_user == NULL && off_user == NULL) {
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

	job = g_malloc0(sizeof(*job));
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

	g_free(job);

done:
	imapx_sync_free_user(on_user);
	imapx_sync_free_user(off_user);

	camel_folder_free_uids (folder, uids);

	return success;
}

gboolean
camel_imapx_server_sync_changes(CamelIMAPXServer *is, CamelFolder *folder, GError **error)
{
	return imapx_server_sync_changes (is, folder, IMAPX_PRIORITY_SYNC_CHANGES, error);
}

/* expunge-uids? */
gboolean
camel_imapx_server_expunge(CamelIMAPXServer *is, CamelFolder *folder, GError **error)
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

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_EXPUNGE;
	job->start = imapx_job_expunge_start;
	job->pri = IMAPX_PRIORITY_EXPUNGE;
	job->folder = folder;

	registered = imapx_register_job (is, job, error);

	QUEUE_UNLOCK (is);

	success = registered && imapx_run_job (is, job, error);

	g_free(job);

	return success;
}

static guint
imapx_name_hash(gconstpointer key)
{
	if (g_ascii_strcasecmp(key, "INBOX") == 0)
		return g_str_hash("INBOX");
	else
		return g_str_hash(key);
}

static gint
imapx_name_equal(gconstpointer a, gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	if (g_ascii_strcasecmp(a, "INBOX") == 0)
		aname = "INBOX";
	if (g_ascii_strcasecmp(b, "INBOX") == 0)
		bname = "INBOX";
	return g_str_equal(aname, bname);
}

static void
imapx_list_flatten(gpointer k, gpointer v, gpointer d)
{
	GPtrArray *folders = d;

	g_ptr_array_add(folders, v);
}

static gint
imapx_list_cmp(gconstpointer ap, gconstpointer bp)
{
	struct _list_info *a = ((struct _list_info **)ap)[0];
	struct _list_info *b = ((struct _list_info **)bp)[0];

	return strcmp(a->name, b->name);
}

GPtrArray *
camel_imapx_server_list (CamelIMAPXServer *is,
                         const gchar *top,
                         guint32 flags,
                         const gchar *ext,
                         GError **error)
{
	CamelIMAPXJob *job;
	GPtrArray *folders = NULL;
	gchar *encoded_name;

	encoded_name = camel_utf8_utf7 (top);

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_LIST;
	job->start = imapx_job_list_start;
	job->pri = IMAPX_PRIORITY_LIST;
	job->u.list.ext = ext;
	job->u.list.flags = flags;
	job->u.list.folders = g_hash_table_new(imapx_name_hash, imapx_name_equal);
	job->u.list.pattern = g_alloca(strlen(encoded_name)+5);
	if (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)
		sprintf(job->u.list.pattern, "%s*", encoded_name);
	else
		sprintf(job->u.list.pattern, "%s", encoded_name);

	/* sync operation which is triggered by user */
	if (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST)
		job->pri += 300;

	if (imapx_submit_job (is, job, error)) {
		folders = g_ptr_array_new();
		g_hash_table_foreach(job->u.list.folders, imapx_list_flatten, folders);
		qsort(folders->pdata, folders->len, sizeof(folders->pdata[0]), imapx_list_cmp);
	}

	g_hash_table_destroy(job->u.list.folders);
	g_free (encoded_name);
	g_free(job);

	return folders;
}

gboolean
camel_imapx_server_manage_subscription (CamelIMAPXServer *is, const gchar *folder_name, gboolean subscribe, GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_MANAGE_SUBSCRIPTION;
	job->start = imapx_job_manage_subscription_start;
	job->pri = IMAPX_PRIORITY_MANAGE_SUBSCRIPTION;
	job->u.manage_subscriptions.subscribe = subscribe;
	job->u.manage_subscriptions.folder_name = folder_name;

	success = imapx_submit_job (is, job, error);

	g_free (job);

	return success;
}

gboolean
camel_imapx_server_create_folder (CamelIMAPXServer *is, const gchar *folder_name, GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_CREATE_FOLDER;
	job->start = imapx_job_create_folder_start;
	job->pri = IMAPX_PRIORITY_CREATE_FOLDER;
	job->u.folder_name = folder_name;

	success = imapx_submit_job (is, job, error);

	g_free (job);

	return success;
}

gboolean
camel_imapx_server_delete_folder (CamelIMAPXServer *is, const gchar *folder_name, GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_DELETE_FOLDER;
	job->start = imapx_job_delete_folder_start;
	job->pri = IMAPX_PRIORITY_DELETE_FOLDER;
	job->u.folder_name = folder_name;

	success = imapx_submit_job (is, job, error);

	g_free (job);

	return success;
}

gboolean
camel_imapx_server_rename_folder (CamelIMAPXServer *is, const gchar *old_name, const gchar *new_name, GError **error)
{
	CamelIMAPXJob *job;
	gboolean success;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_RENAME_FOLDER;
	job->start = imapx_job_rename_folder_start;
	job->pri = IMAPX_PRIORITY_RENAME_FOLDER;
	job->u.rename_folder.ofolder_name = old_name;
	job->u.rename_folder.nfolder_name = new_name;

	success = imapx_submit_job (is, job, error);

	g_free (job);

	return success;
}

IMAPXJobQueueInfo *
camel_imapx_server_get_job_queue_info (CamelIMAPXServer *is)
{
	IMAPXJobQueueInfo *jinfo = g_new0 (IMAPXJobQueueInfo, 1);
	CamelDListNode *node;
	CamelIMAPXJob *job = NULL;

	QUEUE_LOCK(is);

	jinfo->queue_len = camel_dlist_length (&is->jobs);
	jinfo->folders = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);

	for (node = is->jobs.head;node->next;node = job->msg.ln.next) {
		job = (CamelIMAPXJob *) node;

		if (job->folder) {
			const gchar *full_name = camel_folder_get_full_name (job->folder);
			g_hash_table_insert (jinfo->folders, g_strdup (full_name), GINT_TO_POINTER (1));
		}
	}

	if (is->select_folder)
		g_hash_table_insert (jinfo->folders, g_strdup (camel_folder_get_full_name (is->select_folder)), GINT_TO_POINTER (1));

	QUEUE_UNLOCK(is);

	return jinfo;
}

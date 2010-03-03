
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

// fixme, use own type funcs
#include <ctype.h>

#ifdef HAVE_NSS
#include <nspr.h>
#include <prio.h>
#include <prerror.h>
#include <prerr.h>
#endif

#include <poll.h>

#include <camel/camel-list-utils.h>
#include <camel/camel-msgport.h>
#include <camel/camel-object.h>
#include <camel/camel-url.h>
#include <camel/camel-session.h>
#include <camel/camel-stream-fs.h>
#include <camel/camel-stream-null.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-mime-filter-canon.h>
#include <camel/camel-mime-message.h>
#include "camel/camel-string-utils.h"
#include <camel/camel-net-utils.h>
#include "camel/camel-private.h"
#include <camel/camel-tcp-stream-ssl.h>
#include <camel/camel-tcp-stream-raw.h>
#include <camel/camel-db.h>

#include <camel/camel-sasl.h>
#include <camel/camel-i18n.h>
#include <camel/camel-file-utils.h>

#include "camel-imapx-utils.h"
#include "camel-imapx-exception.h"
#include "camel-imapx-stream.h"
#include "camel-imapx-server.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-store.h"
#include "camel-imapx-summary.h"

#define c(x)
#define e(x) 

#define CFS_CLASS(x) ((CamelFolderSummaryClass *)((CamelObject *)x)->klass)

#define CIF(x) ((CamelIMAPXFolder *)x)

#define QUEUE_LOCK(x) (g_static_rec_mutex_lock(&(x)->queue_lock))
#define QUEUE_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->queue_lock))

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

struct _CamelIMAPXCommand;
void imapx_uidset_init(struct _uidset_state *ss, gint total, gint limit);
gint imapx_uidset_done(struct _uidset_state *ss, struct _CamelIMAPXCommand *ic);
gint imapx_uidset_add(struct _uidset_state *ss, struct _CamelIMAPXCommand *ic, const gchar *uid);
static gboolean imapx_disconnect (CamelIMAPXServer *is);
static gint imapx_uid_cmp(gconstpointer ap, gconstpointer bp, gpointer data);

typedef struct _CamelIMAPXCommandPart CamelIMAPXCommandPart;
typedef struct _CamelIMAPXCommand CamelIMAPXCommand;

typedef enum {
	CAMEL_IMAPX_COMMAND_SIMPLE = 0,
	CAMEL_IMAPX_COMMAND_DATAWRAPPER,
	CAMEL_IMAPX_COMMAND_STREAM,
	CAMEL_IMAPX_COMMAND_AUTH,
	CAMEL_IMAPX_COMMAND_FILE,
	CAMEL_IMAPX_COMMAND_STRING,
	CAMEL_IMAPX_COMMAND_MASK = 0xff,
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

	gchar pri;

	const gchar *name;	/* command name/type (e.g. FETCH) */

	gchar *select;		/* folder to select */

	struct _status_info *status; /* status for command, indicates it is complete if != NULL */

	/* If exception is set, it means we were not able to parse above status, it might be
	   because user cancelled the operation or io error */
	CamelException *ex;

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

CamelIMAPXCommand *camel_imapx_command_new(const gchar *name, const gchar *select, const gchar *fmt, ...);
void camel_imapx_command_add(CamelIMAPXCommand *ic, const gchar *fmt, ...);
void camel_imapx_command_free(CamelIMAPXCommand *ic);
void camel_imapx_command_close(CamelIMAPXCommand *ic);
static gboolean imapx_is_command_queue_empty (CamelIMAPXServer *is);

/* states for the connection? */
enum {
	IMAPX_DISCONNECTED,
	IMAPX_CONNECTED,
	IMAPX_AUTHENTICATED,
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
};

struct _imapx_flag_change {
	GPtrArray *infos;
	gchar *name;
};

typedef struct _CamelIMAPXJob CamelIMAPXJob;
struct _CamelIMAPXJob {
	CamelMsg msg;

	CamelException *ex;

	void (*start)(CamelIMAPXServer *is, struct _CamelIMAPXJob *job);

	// ??
	//CamelOperation *op;

	gint noreply:1;		/* dont wait for reply */
	guint32 type;		/* operation type */
	gchar pri;		/* the command priority */
	short commands;		/* counts how many commands are outstanding */

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
			GHashTable *folders;
		} list;
	} u;
};

static void imapx_job_done (CamelIMAPXServer *is, CamelIMAPXJob *job);
static void imapx_run_job (CamelIMAPXServer *is, CamelIMAPXJob *job);
static void imapx_job_fetch_new_messages_start (CamelIMAPXServer *is, CamelIMAPXJob *job);
static void imapx_command_copy_messages_step_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic);
static gint imapx_refresh_info_uid_cmp(gconstpointer ap, gconstpointer bp);
static gint imapx_uids_array_cmp (gconstpointer ap, gconstpointer bp);

typedef struct _CamelIMAPXIdle CamelIMAPXIdle;
struct _CamelIMAPXIdle {
	GMutex *idle_lock;
	EFlag *idle_start_watch;
	GThread *idle_thread;

	gboolean idle_issue_done;
	gboolean in_idle;
	gboolean started;
	gboolean idle_exit;
};

static gboolean imapx_in_idle (CamelIMAPXServer *is);
static gboolean imapx_idle_supported (CamelIMAPXServer *is);
static void imapx_start_idle (CamelIMAPXServer *is);
static void imapx_exit_idle (CamelIMAPXServer *is);
static void imapx_init_idle (CamelIMAPXServer *is);
static void imapx_stop_idle (CamelIMAPXServer *is, CamelException *ex);
static void camel_imapx_server_idle (CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex);

enum {
	USE_SSL_NEVER,
	USE_SSL_ALWAYS,
	USE_SSL_WHEN_POSSIBLE
};

#define SSL_PORT_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 | CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
#define STARTTLS_FLAGS (CAMEL_TCP_STREAM_SSL_ENABLE_TLS)

static void imapx_select(CamelIMAPXServer *is, CamelFolder *folder, gboolean force, CamelException *ex);

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
	guint ob_size = 0;

	/* TODO: literal+? */

	switch (type & CAMEL_IMAPX_COMMAND_MASK) {
	case CAMEL_IMAPX_COMMAND_DATAWRAPPER:
	case CAMEL_IMAPX_COMMAND_STREAM: {
		CamelObject *ob = o;

		/* TODO: seekable streams we could just seek to the end and back */
		null = (CamelStreamNull *)camel_stream_null_new();
		if ( (type & CAMEL_IMAPX_COMMAND_MASK) == CAMEL_IMAPX_COMMAND_DATAWRAPPER) {
			camel_data_wrapper_write_to_stream((CamelDataWrapper *)ob, (CamelStream *)null);
		} else {
			camel_stream_reset((CamelStream *)ob);
			camel_stream_write_to_stream((CamelStream *)ob, (CamelStream *)null);
			camel_stream_reset((CamelStream *)ob);
		}
		type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		camel_object_ref(ob);
		ob_size = null->written;
		camel_object_unref((CamelObject *)null);
		camel_stream_printf((CamelStream *)ic->mem, "{%u}", ob_size);
		break;
	}
	case CAMEL_IMAPX_COMMAND_AUTH: {
		CamelObject *ob = o;

		/* we presume we'll need to get additional data only if we're not authenticated yet */
		camel_object_ref(ob);
		camel_stream_printf((CamelStream *)ic->mem, "%s", ((CamelSasl *)ob)->mech);
		if (!camel_sasl_authenticated((CamelSasl *)ob))
			type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		break;
	}
	case CAMEL_IMAPX_COMMAND_FILE: {
		gchar *path = o;
		struct stat st;

		if (stat(path, &st) == 0) {
			o = g_strdup(o);
			ob_size = st.st_size;
		} else
			o = NULL;

		camel_stream_printf((CamelStream *)ic->mem, "{%u}", ob_size);
		type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		break;
	}
	case CAMEL_IMAPX_COMMAND_STRING:
		o = g_strdup(o);
		ob_size = strlen(o);
		camel_stream_printf((CamelStream *)ic->mem, "{%u}", ob_size);
		type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		break;
	default:
		ob_size = 0;
	}

	cp = g_malloc0(sizeof(*cp));
	cp->type = type;
	cp->ob_size = ob_size;
	cp->ob = o;
	cp->data_size = ic->mem->buffer->len;
	cp->data = g_malloc(cp->data_size+1);
	memcpy(cp->data, ic->mem->buffer->data, cp->data_size);
	cp->data[cp->data_size] = 0;

	camel_stream_reset((CamelStream *)ic->mem);
	/* FIXME: hackish? */
	g_byte_array_set_size(ic->mem->buffer, 0);

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
	CamelException ex = CAMEL_EXCEPTION_INITIALISER;

	c(printf("adding command, fmt = '%s'\n", fmt));

	p = fmt;
	ps = fmt;
	while (( c = *p++ )) {
		switch (c) {
		case '%':
			if (*p == '%') {
				camel_stream_write((CamelStream *)ic->mem, ps, p-ps);
				p++;
				ps = p;
			} else {
				camel_stream_write((CamelStream *)ic->mem, ps, p-ps-1);
				start = p-1;
				width = 0;
				left = FALSE;
				fill = FALSE;
				zero = FALSE;
				llong = FALSE;

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

				if (c == 'l') {
					llong = TRUE;
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
					camel_stream_write((CamelStream *)ic->mem, s, strlen(s));
					break;
				case 's': /* simple string */
					s = va_arg(ap, gchar *);
					c(printf("got string '%s'\n", s));
					if (*s) {
						guchar mask = imapx_is_mask(s);

						if (mask & IMAPX_TYPE_ATOM_CHAR)
							camel_stream_write((CamelStream *)ic->mem, s, strlen(s));
						else if (mask & IMAPX_TYPE_TEXT_CHAR) {
							camel_stream_write((CamelStream *)ic->mem, "\"", 1);
							while (*s) {
								gchar *start = s;

								while (*s && imapx_is_quoted_char(*s))
									s++;
								camel_stream_write((CamelStream *)ic->mem, start, s-start);
								if (*s) {
									camel_stream_write((CamelStream *)ic->mem, "\\", 1);
									camel_stream_write((CamelStream *)ic->mem, s, 1);
									s++;
								}
							}
							camel_stream_write((CamelStream *)ic->mem, "\"", 1);
						} else {
							imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_STRING, s);
						}
					} else {
						camel_stream_write((CamelStream *)ic->mem, "\"\"", 2);
					}
					break;
				case 'f': /* imap folder name */
					s = va_arg(ap, gchar *);
					c(printf("got folder '%s'\n", s));
					/* FIXME: encode folder name */
					/* FIXME: namespace? */
					camel_stream_printf((CamelStream *)ic->mem, "\"%s\"", s?s:"");
					break;
				case 'F': /* IMAP flags set */
					f = va_arg(ap, guint32);
					F = va_arg(ap, CamelFlag *);
					imapx_write_flags((CamelStream *)ic->mem, f, F, &ex);
					break;
				case 'c':
					d = va_arg(ap, gint);
					ch = d;
					camel_stream_write((CamelStream *)ic->mem, &ch, 1);
					break;
				case 'd': /* int/unsigned */
				case 'u':
					if (llong) {
						l = va_arg(ap, glong);
						c(printf("got glong '%d'\n", (gint)l));
						memcpy(buffer, start, p-start);
						buffer[p-start] = 0;
						camel_stream_printf((CamelStream *)ic->mem, buffer, l);
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
				camel_stream_write((CamelStream *)ic->mem, ps, p-ps);
				p++;
				ps = p;
			}
		}
	}

	camel_stream_write((CamelStream *)ic->mem, ps, p-ps-1);
}

CamelIMAPXCommand *
camel_imapx_command_new(const gchar *name, const gchar *select, const gchar *fmt, ...)
{
	CamelIMAPXCommand *ic;
	static gint tag = 0;
	va_list ap;

	ic = g_malloc0(sizeof(*ic));
	ic->tag = tag++;
	ic->name = name;
	ic->mem = (CamelStreamMem *)camel_stream_mem_new();
	ic->select = g_strdup(select);
	camel_dlist_init(&ic->parts);
	ic->ex = camel_exception_new ();

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
		camel_object_unref((CamelObject *)ic->mem);
	imapx_free_status(ic->status);
	g_free(ic->select);

	while ((cp = ((CamelIMAPXCommandPart *)camel_dlist_remhead(&ic->parts)))) {
		g_free(cp->data);
		if (cp->ob) {
			switch (cp->type & CAMEL_IMAPX_COMMAND_MASK) {
			case CAMEL_IMAPX_COMMAND_FILE:
			case CAMEL_IMAPX_COMMAND_STRING:
				g_free(cp->ob);
				break;
			default:
				camel_object_unref(cp->ob);
			}
		}
		g_free(cp);
	}

	camel_exception_free (ic->ex);
	g_free(ic);
}

void
camel_imapx_command_close(CamelIMAPXCommand *ic)
{
	if (ic->mem) {
		c(printf("completing command buffer is [%d] '%.*s'\n", ic->mem->buffer->len, (gint)ic->mem->buffer->len, ic->mem->buffer->data));
		if (ic->mem->buffer->len > 0)
			imapx_command_add_part(ic, CAMEL_IMAPX_COMMAND_SIMPLE, NULL);

		camel_object_unref((CamelObject *)ic->mem);
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

	if (cp->type & CAMEL_IMAPX_COMMAND_CONTINUATION)
		imap->literal = ic;

	camel_dlist_addtail(&imap->active, (CamelDListNode *)ic);

	g_static_rec_mutex_lock (&imap->ostream_lock);
	
	c(printf("Staring command (active=%d,%s) %c%05u %s\r\n", camel_dlist_length(&imap->active), imap->literal?" literal":"", imap->tagprefix, ic->tag, cp->data));
	if (!imap->stream || camel_stream_printf((CamelStream *)imap->stream, "%c%05u %s\r\n", imap->tagprefix, ic->tag, cp->data) == -1) {
		g_static_rec_mutex_unlock (&imap->ostream_lock);

		camel_exception_set (ic->ex, 1, "Failed to issue the command");
		camel_dlist_remove ((CamelDListNode *)ic);
		ic->complete (imap, ic);
		return FALSE;
	}
	
	g_static_rec_mutex_unlock (&imap->ostream_lock);
	
	return TRUE;
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
imapx_command_start_next(CamelIMAPXServer *is, CamelException *ex)
{
	CamelIMAPXCommand *ic, *nc;
	gint count = 0;
	gint pri = -128;

	c(printf("** Starting next command\n"));
	if (is->literal != NULL || is->select_pending != NULL) {
		c(printf("* no, waiting for literal/pending select '%s'\n", is->select_pending->full_name));
		return;
	}

	if (imapx_idle_supported (is) && is->state == IMAPX_SELECTED) {
		gboolean empty = imapx_is_command_queue_empty (is);

		if (imapx_in_idle (is) && !camel_dlist_empty (&is->queue)) {
			imapx_stop_idle (is, ex);
			c(printf ("waiting for idle to stop \n"));
			return;
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
	if (is->select) {
		c(printf("- we're selected on '%s', current jobs?\n", is->select));
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
			if (ic->select == NULL || strcmp(ic->select, is->select) == 0) {
				c(printf("--> starting '%s'\n", ic->name));
				pri = ic->pri;
				camel_dlist_remove((CamelDListNode *)ic);
				imapx_command_start(is, ic);
				count++;
			} else
				break;
			ic = nc;
			nc = nc->next;
		}

		if (count)
			return;

		ic = (CamelIMAPXCommand *)is->queue.head;
	}

	/* If we need to select a folder for the first command, do it now, once
	   it is complete it will re-call us if it succeeded */
	if (ic->job->folder) {
		imapx_select(is, ic->job->folder, FALSE, ex);
	} else {
		pri = ic->pri;
		nc = ic->next;
		count = 0;
		while (nc && is->literal == NULL && count < MAX_COMMANDS && ic->pri >= pri) {
			if (ic->select == NULL || (is->select && strcmp(ic->select, is->select))) {
				c(printf("* queueing job %3d '%s'\n", (gint)ic->pri, ic->name));
				pri = ic->pri;
				camel_dlist_remove((CamelDListNode *)ic);
				if (!imapx_command_start(is, ic)) {
					QUEUE_UNLOCK (is);
					ic->complete (is, ic);
					QUEUE_LOCK (is);
				}
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
found:
	QUEUE_UNLOCK (imap);

	return ic;
}

static gboolean
imapx_job_matches (const gchar *folder_name, CamelIMAPXJob *job, guint32 type, const gchar *uid)
{
	switch (job->type) {
		case IMAPX_JOB_GET_MESSAGE:
			if (folder_name	&& strcmp(job->folder->full_name, folder_name) == 0
					&& strcmp(job->u.get_message.uid, uid) == 0)
				return TRUE;
			break;
		case IMAPX_JOB_FETCH_NEW_MESSAGES:
		case IMAPX_JOB_REFRESH_INFO:
		case IMAPX_JOB_SYNC_CHANGES:
		case IMAPX_JOB_EXPUNGE:
			if (folder_name
					&& strcmp(job->folder->full_name, folder_name) == 0)
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

		if (imapx_job_matches (is->select, job, type, uid))
			goto found;
	}
	job = NULL;
found:
	QUEUE_UNLOCK(is);
	return job;
}

static gboolean
imapx_is_job_in_queue (CamelIMAPXServer *is, const gchar *folder_name, guint32 type, const char *uid)
{
	CamelDListNode *node;
	CamelIMAPXJob *job = NULL;
	gboolean found = FALSE;

	QUEUE_LOCK(is);

	for (node = is->jobs.head;node->next;node = job->msg.ln.next) {
		job = (CamelIMAPXJob *) node;
		
		if (!job || !(job->type & type))
			continue;

		if (imapx_job_matches (folder_name, job, type, uid)) {
			found = TRUE;
			break;	
		}
	}

	QUEUE_UNLOCK (is);
	return found;
}

/* handle any untagged responses */
static gint
imapx_untagged(CamelIMAPXServer *imap, CamelException *ex)
{
	guint id, len;
	guchar *token, *p, c;
	gint tok;
	gboolean lsub = FALSE;
	struct _status_info *sinfo;

	e(printf("got untagged response\n"));
	id = 0;
	tok = camel_imapx_stream_token(imap->stream, &token, &len, ex);
	if (camel_exception_is_set (ex))
		return -1;

	if (tok == IMAPX_TOK_INT) {
		id = strtoul((gchar *) token, NULL, 10);
		tok = camel_imapx_stream_token(imap->stream, &token, &len, ex);
	}

	if (tok == '\n') {
		camel_exception_set (ex, 1, "truncated server response");
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
		imap->cinfo = imapx_parse_capability(imap->stream, ex);
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
			CamelMessageInfo *mi;

			uid = camel_folder_summary_uid_from_index (imap->select_folder->summary, expunge - 1);
			if (!uid)
				break;

			if (imap->changes == NULL)
				imap->changes = camel_folder_change_info_new();

			mi = camel_folder_summary_uid (imap->select_folder->summary, uid);
			if (mi) {
				imapx_update_summary_for_removed_message (mi, imap->select_folder);
				camel_message_info_free (mi);
			}

			camel_folder_summary_remove_uid_fast (imap->select_folder->summary, uid);
			imap->expunged = g_slist_prepend (imap->expunged, uid);

			camel_folder_change_info_remove_uid (imap->changes, uid);

			if (imapx_idle_supported (imap) && imapx_in_idle (imap)) {
				camel_db_delete_uids (imap->store->cdb_w, imap->select_folder->full_name, imap->expunged, NULL);
				imapx_update_store_summary (imap->select_folder);
				camel_object_trigger_event(imap->select_folder, "folder_changed", imap->changes);

				g_slist_foreach (imap->expunged, (GFunc) g_free, NULL);
				imap->expunged = NULL;
				camel_folder_change_info_clear (imap->changes);
			}
		}

		break;
	}
	case IMAPX_NAMESPACE: {
		CamelIMAPXNamespaceList *nsl = NULL;

		nsl = imapx_parse_namespace_list (imap->stream, ex);
		if (nsl != NULL) {
			CamelIMAPXStore *imapx_store = (CamelIMAPXStore *) imap->store;

			imapx_store->summary->namespaces = nsl;
			camel_store_summary_touch ((CamelStoreSummary *) imapx_store->summary);
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
				imapx_stop_idle (imap, ex);
		}

		break;
	case IMAPX_FLAGS: {
		guint32 flags;

		imapx_parse_flags(imap->stream, &flags, NULL, ex);

		c(printf("flags: %08x\n", flags));
		break;
	}
	case IMAPX_FETCH: {
		struct _fetch_info *finfo;

		finfo = imapx_parse_fetch(imap->stream, ex);
		if (camel_exception_is_set (ex)) {
			imapx_free_fetch(finfo);
			return -1;
		}

		if ((finfo->got & (FETCH_BODY|FETCH_UID)) == (FETCH_BODY|FETCH_UID)) {
			CamelIMAPXJob *job = imapx_match_active_job(imap, IMAPX_JOB_GET_MESSAGE, finfo->uid);

			/* This must've been a get-message request, fill out the body stream,
			   in the right spot */

			if (job) {
				if (job->u.get_message.use_multi_fetch) {
					job->u.get_message.body_offset = finfo->offset;
					camel_seekable_stream_seek((CamelSeekableStream *)job->u.get_message.stream, finfo->offset, CAMEL_STREAM_SET);
				}

				job->u.get_message.body_len = camel_stream_write_to_stream(finfo->body, job->u.get_message.stream);
				if (job->u.get_message.body_len == -1) {
					camel_exception_setv(job->ex, 1, "error writing to cache stream: %s\n", g_strerror(errno));
					camel_object_unref(job->u.get_message.stream);
					job->u.get_message.stream = NULL;
				}
			}
		}

		if (finfo->got & FETCH_FLAGS && !(finfo->got & FETCH_UID)) {
			if (imap->select_folder) {
				CamelFolder *folder;
				CamelMessageInfo *mi = NULL;
				gboolean changed = FALSE;
				gchar *uid = NULL;

				camel_object_ref(imap->select_folder);
				folder = imap->select_folder;

				c(printf("flag changed: %d\n", id));

				if ( (uid = camel_folder_summary_uid_from_index (folder->summary, id - 1)))
				{
					mi = camel_folder_summary_uid (folder->summary, uid);
					if (mi)
						changed = imapx_update_message_info_flags (mi, finfo->flags, finfo->user_flags, folder);
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
					camel_object_trigger_event(imap->select_folder, "folder_changed", imap->changes);
					camel_folder_change_info_clear (imap->changes);
				}

				if (mi)
					camel_message_info_free (mi);
				camel_object_unref (folder);
			}
		}

		if ((finfo->got & (FETCH_FLAGS|FETCH_UID)) == (FETCH_FLAGS|FETCH_UID) && !(finfo->got & FETCH_HEADER)) {
			CamelIMAPXJob *job = imapx_match_active_job (imap, IMAPX_JOB_FETCH_NEW_MESSAGES|IMAPX_JOB_REFRESH_INFO, NULL);

			/* This is either a refresh_info job, check to see if it is and update
			   if so, otherwise it must've been an unsolicited response, so update
			   the summary to match */

			if (job) {
				struct _refresh_info r;

				r.uid = finfo->uid;
				finfo->uid = NULL;
				r.server_flags = finfo->flags;
				r.server_user_flags = finfo->user_flags;
				finfo->user_flags = NULL;
				r.exists = FALSE;
				g_array_append_val(job->u.refresh_info.infos, r);
			} else {
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
				camel_mime_parser_init_with_stream(mp, finfo->header);
				mi = camel_folder_summary_info_new_from_parser(job->folder->summary, mp);
				camel_object_unref(mp);

				if (mi) {
					guint32 server_flags;
					CamelFlag *server_user_flags;
					CamelMessageInfoBase *binfo;

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
					}

					binfo = (CamelMessageInfoBase *) mi;
					binfo->size = finfo->size;

					if (!camel_folder_summary_check_uid (job->folder->summary, mi->uid)) {
						camel_folder_summary_add(job->folder->summary, mi);
						imapx_set_message_info_flags_for_new_message (mi, server_flags, server_user_flags, job->folder);
						camel_folder_change_info_add_uid (job->u.refresh_info.changes, mi->uid);

						if (job->op)
							camel_operation_progress (job->op, (camel_folder_summary_count (job->folder->summary) * 100)/imap->exists);
					}
				}
			}
		}

		imapx_free_fetch(finfo);
		break;
	}
	case IMAPX_LSUB:
		lsub = TRUE;
	case IMAPX_LIST: {
		struct _list_info *linfo = imapx_parse_list(imap->stream, ex);
		CamelIMAPXJob *job = imapx_match_active_job(imap, IMAPX_JOB_LIST, linfo->name);

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
		struct _state_info *sinfo = imapx_parse_status_info (imap->stream, ex);
		if (sinfo) {
			/* this is what we use atm */
			imap->exists = sinfo->messages;
			imap->unread = sinfo->unseen;

			g_free (sinfo);
		}
		break;
	}
	case IMAPX_BYE: case IMAPX_OK: case IMAPX_NO: case IMAPX_BAD: case IMAPX_PREAUTH:
		/* TODO: validate which ones of these can happen as unsolicited responses */
		/* TODO: handle bye/preauth differently */
		camel_imapx_stream_ungettoken(imap->stream, tok, token, len);
		sinfo = imapx_parse_status(imap->stream, ex);
		camel_object_trigger_event(imap, "status", sinfo);
		switch (sinfo->condition) {
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
		case IMAPX_PERMANENTFLAGS:
			imap->permanentflags = sinfo->u.permanentflags;
			break;
		case IMAPX_ALERT:
			c(printf("ALERT!: %s\n", sinfo->text));
			break;
		case IMAPX_PARSE:
			c(printf("PARSE: %s\n", sinfo->text));
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

	return camel_imapx_stream_skip(imap->stream, ex);
}

/* handle any continuation requests
   either data continuations, or auth continuation */
static gint
imapx_continuation(CamelIMAPXServer *imap, CamelException *ex)
{
	CamelIMAPXCommand *ic, *newliteral = NULL;
	CamelIMAPXCommandPart *cp;

	c(printf("got continuation response\n"));

	g_static_rec_mutex_lock (&imap->istream_lock);
	/* The 'literal' pointer is like a write-lock, nothing else
	   can write while we have it ... so we dont need any
	   ohter lock here.  All other writes go through
	   queue-lock */
	if (imapx_idle_supported (imap) && imapx_in_idle (imap)) {
		camel_imapx_stream_skip (imap->stream, ex);

		g_static_rec_mutex_unlock (&imap->istream_lock);
		
		c(printf("Got continuation response for IDLE \n"));
		imap->idle->started = TRUE;

		QUEUE_LOCK(imap);
		imap->literal = NULL;
		imapx_command_start_next(imap, ex);
		QUEUE_UNLOCK(imap);

		return 1;
	}

	ic = imap->literal;
	if (ic == NULL) {
		camel_imapx_stream_skip(imap->stream, ex);
		g_static_rec_mutex_unlock (&imap->istream_lock);
		c(printf("got continuation response with no outstanding continuation requests?\n"));
		return 1;
	}

	c(printf("got continuation response for data\n"));
	cp = ic->current;
	switch (cp->type & CAMEL_IMAPX_COMMAND_MASK) {
	case CAMEL_IMAPX_COMMAND_DATAWRAPPER:
		c(printf("writing data wrapper to literal\n"));
		camel_data_wrapper_write_to_stream((CamelDataWrapper *)cp->ob, (CamelStream *)imap->stream);
		break;
	case CAMEL_IMAPX_COMMAND_STREAM:
		c(printf("writing stream to literal\n"));
		camel_stream_write_to_stream((CamelStream *)cp->ob, (CamelStream *)imap->stream);
		break;
	case CAMEL_IMAPX_COMMAND_AUTH: {
		gchar *resp;
		guchar *token;
		gint tok;
		guint len;

		tok = camel_imapx_stream_token(imap->stream, &token, &len, ex);
		resp = camel_sasl_challenge_base64((CamelSasl *)cp->ob, (const gchar *) token, ex);
		if (camel_exception_is_set(ex))
			return -1;

		c(printf("got auth continuation, feeding token '%s' back to auth mech\n", resp));

		camel_stream_write((CamelStream *)imap->stream, resp, strlen(resp));

		/* we want to keep getting called until we get a status reponse from the server
		   ignore what sasl tells us */
		newliteral = ic;

		break; }
	case CAMEL_IMAPX_COMMAND_FILE: {
		CamelStream *file;

		c(printf("writing file '%s' to literal\n", (gchar *)cp->ob));

		// FIXME: errors
		if (cp->ob && (file = camel_stream_fs_new_with_name(cp->ob, O_RDONLY, 0))) {
			camel_stream_write_to_stream(file, (CamelStream *)imap->stream);
			camel_object_unref(file);
		} else if (cp->ob_size > 0) {
			// Server is expecting data ... ummm, send it zeros?  abort?
		}
		break; }
	case CAMEL_IMAPX_COMMAND_STRING:
		camel_stream_write((CamelStream *)imap->stream, cp->ob, cp->ob_size);
		break;
	default:
		/* should we just ignore? */
		imap->literal = NULL;
		camel_exception_set (ex, 1, "continuation response for non-continuation request");
		g_static_rec_mutex_unlock (&imap->istream_lock);
		return -1;
	}

	camel_imapx_stream_skip(imap->stream, ex);

	cp = cp->next;
	if (cp->next) {
		ic->current = cp;
		c(printf("next part of command \"A%05u: %s\"\n", ic->tag, cp->data));
		camel_stream_printf((CamelStream *)imap->stream, "%s\r\n", cp->data);
		if (cp->type & CAMEL_IMAPX_COMMAND_CONTINUATION) {
			newliteral = ic;
		} else {
			g_assert(cp->next->next == NULL);
		}
	} else {
		c(printf("%p: queueing continuation\n", ic));
		camel_stream_printf((CamelStream *)imap->stream, "\r\n");
	}
	
	g_static_rec_mutex_unlock (&imap->istream_lock);

	QUEUE_LOCK(imap);
	imap->literal = newliteral;

	imapx_command_start_next(imap, ex);
	QUEUE_UNLOCK(imap);

	return 1;
}

/* handle a completion line */
static gint
imapx_completion(CamelIMAPXServer *imap, guchar *token, gint len, CamelException *ex)
{
	CamelIMAPXCommand *ic;
	guint tag;

	if (token[0] != imap->tagprefix) {
		camel_exception_setv (ex, 1, "Server sent unexpected response: %s", token);

		return -1;
	}

	tag = strtoul( (const gchar *)token+1, NULL, 10);

	if ((ic = imapx_find_command_tag(imap, tag)) == NULL) {
		camel_exception_setv (ex, 1, "got response tag unexpectedly: %s", token);

		return -1;
	}

	c(printf("Got completion response for command %05u '%s'\n", ic->tag, ic->name));

	if (camel_folder_change_info_changed (imap->changes)) {
		if (imap->changes->uid_changed->len)
			camel_folder_summary_save_to_db (imap->select_folder->summary, NULL);
		else
			camel_db_delete_uids (imap->store->cdb_w, imap->select_folder->full_name, imap->expunged, NULL);

		if (imap->expunged) {
			g_slist_foreach (imap->expunged, (GFunc) g_free, NULL);
			imap->expunged = NULL;
		}

		imapx_update_store_summary (imap->select_folder);
		camel_object_trigger_event(imap->select_folder, "folder_changed", imap->changes);
		camel_folder_change_info_clear (imap->changes);
	}

	QUEUE_LOCK(imap);

	camel_dlist_remove((CamelDListNode *)ic);
	camel_dlist_addtail(&imap->done, (CamelDListNode *)ic);
	if (imap->literal == ic)
		imap->literal = NULL;

	if (ic->current->next->next) {
		QUEUE_UNLOCK(imap);
		camel_exception_setv (ex, 1, "command still has unsent parts? %s", ic->name);

		return -1;
	}

	/* A follow-on command might've already queued a new literal since were were done with ours? */
//	if (imap->literal != NULL) {
//		QUEUE_UNLOCK(imap);
//		camel_exception_throw(1, "command still has outstanding continuation", imap->literal->name);
//	}

	camel_dlist_remove ((CamelDListNode *) ic);
	QUEUE_UNLOCK(imap);
	
	g_static_rec_mutex_lock (&imap->istream_lock);
	ic->status = imapx_parse_status(imap->stream, ex);
	g_static_rec_mutex_unlock (&imap->istream_lock);

	if (ic->complete)
		ic->complete (imap, ic);

	QUEUE_LOCK(imap);
	imapx_command_start_next(imap, ex);
	QUEUE_UNLOCK(imap);

	return 1;
}

static void
imapx_step(CamelIMAPXServer *is, CamelException *ex)
{
	guint len;
	guchar *token;
	gint tok;

	// poll ?  wait for other stuff? loop?
	g_static_rec_mutex_lock (&is->istream_lock);
	tok = camel_imapx_stream_token (is->stream, &token, &len, ex);
	g_static_rec_mutex_unlock (&is->istream_lock);
	if (camel_exception_is_set (ex))
		return;

	if (tok == '*')
		imapx_untagged (is, ex);
	else if (tok == IMAPX_TOK_TOKEN)
		imapx_completion (is, token, len, ex);
	else if (tok == '+')
		imapx_continuation (is, ex);
	else
		camel_exception_set (ex, 1, "unexpected server response:");
}

/* Used to run 1 command synchronously,
   use for capa, login, and namespaces only. */
static void
imapx_command_run(CamelIMAPXServer *is, CamelIMAPXCommand *ic, CamelException *ex)
/* throws IO,PARSE exception */
{
	camel_imapx_command_close(ic);

	QUEUE_LOCK(is);
	g_assert(camel_dlist_empty(&is->active));
	imapx_command_start(is, ic);
	QUEUE_UNLOCK(is);

	do {
		imapx_step(is, ex);
	} while (ic->status == NULL && !camel_exception_is_set (ex));

	QUEUE_LOCK(is);
	camel_dlist_remove((CamelDListNode *)ic);
	QUEUE_UNLOCK(is);
}

static void
imapx_command_complete (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	e_flag_set (ic->flag);
}

/* change status to a job and remove command_run_sync */
static void
imapx_command_status_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) ic->job->folder;

	ifolder->unread_on_server = is->unread;
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
		camel_exception_clear(job->ex);
		g_free(job);
	} else
		camel_msgport_reply((CamelMsg *) job);
}

static gboolean
imapx_register_job (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	if (is->state >= IMAPX_AUTHENTICATED) {
		QUEUE_LOCK (is);
		camel_dlist_addhead (&is->jobs, (CamelDListNode *)job);
		QUEUE_UNLOCK (is);

	} else {
		e(printf ("NO connection yet, maybe user cancelled jobs earlier ?"));
		camel_exception_set (job->ex, CAMEL_EXCEPTION_SERVICE_NOT_CONNECTED, "Not authenticated");
		return FALSE;
	}

	return TRUE;
}

static void
imapx_run_job (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelMsgPort *reply;

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
}

/* ********************************************************************** */
// IDLE support

#define IDLE_LOCK(x) (g_mutex_lock((x)->idle_lock))
#define IDLE_UNLOCK(x) (g_mutex_unlock((x)->idle_lock))

/*TODO handle negative cases sanely */
static gboolean
imapx_command_idle_stop (CamelIMAPXServer *is, CamelException *ex)
{
	if (!is->stream || camel_stream_printf((CamelStream *)is->stream, "%s", "DONE\r\n") == -1) {
		camel_exception_set (ex, 1, "Unable to issue DONE");
		return FALSE;
	}

	return TRUE;
}

static void
imapx_command_idle_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXIdle *idle = is->idle;

	if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
		if (!camel_exception_is_set (ic->ex))
			camel_exception_setv(ic->job->ex, 1, "Error performing IDLE: %s", ic->status->text);
		else
			camel_exception_xfer (ic->job->ex, ic->ex);
	}

	IDLE_LOCK (idle);
	idle->in_idle = FALSE;
	idle->idle_issue_done = FALSE;
	idle->started = FALSE;
	IDLE_UNLOCK (idle);

	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_idle_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	CamelIMAPXCommandPart *cp;

	ic = camel_imapx_command_new ("IDLE", job->folder->full_name, "IDLE");
	ic->job = job;
	ic->complete = imapx_command_idle_done;

	camel_imapx_command_close(ic);
	cp = (CamelIMAPXCommandPart *)ic->parts.head;
	cp->type |= CAMEL_IMAPX_COMMAND_CONTINUATION;

	QUEUE_LOCK (is);
	imapx_command_start (is, ic);
	QUEUE_UNLOCK (is);
}

static void
camel_imapx_server_idle (CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex)
{
	CamelIMAPXJob *job;

	job = g_malloc0 (sizeof(*job));
	job->type = IMAPX_JOB_IDLE;
	job->start = imapx_job_idle_start;
	job->folder = folder;
	job->ex = ex;

	if (imapx_register_job (is, job))
		imapx_run_job(is, job);
	g_free(job);
}

static void
imapx_server_fetch_new_messages (CamelIMAPXServer *is, CamelFolder *folder, gboolean async, CamelException *ex)
{
	CamelIMAPXJob *job;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_FETCH_NEW_MESSAGES;
	job->start = imapx_job_fetch_new_messages_start;
	job->folder = folder;
	job->noreply = async;
	job->ex = ex;
	job->u.refresh_info.changes = camel_folder_change_info_new();
	job->op = camel_operation_registered ();

	if (imapx_register_job (is, job))
		imapx_run_job (is, job);
}

static gpointer
imapx_idle_thread (gpointer data)
{
	CamelException *ex = camel_exception_new ();
	CamelIMAPXServer *is = (CamelIMAPXServer *) data;

	while (TRUE) {
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) is->select_folder;

		e_flag_clear (is->idle->idle_start_watch);
		camel_imapx_server_idle (is, is->select_folder, ex);

		if (!camel_exception_is_set (ex) && ifolder->exists_on_server >
				camel_folder_summary_count (((CamelFolder *) ifolder)->summary) && imapx_is_command_queue_empty (is))
			imapx_server_fetch_new_messages (is, is->select_folder, TRUE, ex);

		if (camel_exception_is_set (ex)) {
			e(printf ("Caught exception in idle thread:  %s \n", ex->desc));
			/* No way to asyncronously notify UI ? */
			camel_exception_clear (ex);
		}

		e_flag_wait (is->idle->idle_start_watch);

		if (is->idle->idle_exit)
			break;
	}

	camel_exception_free (ex);
	is->idle->idle_thread = NULL;
	return NULL;
}

static void
imapx_stop_idle (CamelIMAPXServer *is, CamelException *ex)
{
	CamelIMAPXIdle *idle = is->idle;

	IDLE_LOCK (idle);

	if (!idle->idle_issue_done && idle->started) {
		imapx_command_idle_stop (is, ex);
		idle->idle_issue_done = TRUE;
	}

	IDLE_UNLOCK (idle);
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

	if (!idle)
		return;

	IDLE_LOCK (idle);

	if (idle->idle_thread) {
		idle->idle_exit = TRUE;
		e_flag_set (idle->idle_start_watch);

		if (idle->idle_thread)
			g_thread_join (idle->idle_thread);
	}

	idle->idle_thread = NULL;
	IDLE_UNLOCK (idle);

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

	if (!idle->idle_thread) {
		idle->idle_start_watch = e_flag_new ();
		idle->idle_thread = g_thread_create ((GThreadFunc) imapx_idle_thread, is, TRUE, NULL);
	} else
		e_flag_set (idle->idle_start_watch);

	idle->in_idle = TRUE;

	IDLE_UNLOCK (idle);
}

static gboolean
imapx_in_idle (CamelIMAPXServer *is)
{
	gboolean ret = FALSE;
	CamelIMAPXIdle *idle = is->idle;

	IDLE_LOCK (idle);
	ret = idle->in_idle;
	IDLE_UNLOCK (idle);

	return ret;
}

static gboolean
imapx_idle_supported (CamelIMAPXServer *is)
{
	return (is->cinfo && is->cinfo->capa & IMAPX_CAPABILITY_IDLE && is->use_idle);
}

// end IDLE
/* ********************************************************************** */
static void
imapx_command_select_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{

	if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
		CamelDList failed;
		CamelIMAPXCommand *cw, *cn;

		c(printf("Select failed\n"));
		camel_dlist_init (&failed);

		QUEUE_LOCK(is);
		cw = (CamelIMAPXCommand *)is->queue.head;
		cn = cw->next;
		
		if (is->select_pending) {
			while (cn) {
				if (cw->select && strcmp(cw->select, is->select_pending->full_name) == 0) {
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
				camel_exception_setv (cw->ex, 1, "select %s failed", cw->select);
				cw->complete(is, cw);
				cw = cn;
				cn = cn->next;
			}
		}

		if (is->select_pending)
			camel_object_unref(is->select_pending);
	} else {
		CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) is->select_pending;
		c(printf("Select ok!\n"));

		is->select_folder = is->select_pending;
		is->select = g_strdup(is->select_folder->full_name);
		is->state = IMAPX_SELECTED;
		ifolder->exists_on_server = is->exists;
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
}

/* Should have a queue lock. TODO Change the way select is written */
static void
imapx_select (CamelIMAPXServer *is, CamelFolder *folder, gboolean forced, CamelException *ex)
{
	CamelIMAPXCommand *ic;

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
		return;

	if (is->select && strcmp(is->select, folder->full_name) == 0 && !forced)
		return;
		
	if (!camel_dlist_empty(&is->active))
		return;

	is->select_pending = folder;
	camel_object_ref(folder);
	if (is->select_folder) {
		g_free(is->select);
		camel_object_unref(is->select_folder);
		is->select = NULL;
		is->select_folder = NULL;
	}

	is->uidvalidity = 0;
	is->unseen = 0;
	is->permanentflags = 0;
	is->exists = 0;
	is->recent = 0;
	is->mode = 0;

	/* Hrm, what about reconnecting? */
	is->state = IMAPX_AUTHENTICATED;

	ic = camel_imapx_command_new("SELECT", NULL, "SELECT %s", CIF(folder)->raw_name);
	ic->complete = imapx_command_select_done;
	imapx_command_start (is, ic);
}

static void
imapx_connect (CamelIMAPXServer *is, gint ssl_mode, gint try_starttls, CamelException *ex)
{
	CamelStream * tcp_stream = NULL;
	CamelSockOptData sockopt;
	gint ret;

#ifdef HAVE_SSL
	const gchar *mode;
#endif
	guchar *buffer = NULL;
	guint len;
	const gchar *serv;
	const gchar *port = NULL;
	struct addrinfo *ai, hints = { 0 };
	CamelIMAPXCommand *ic;

	if (is->url->port) {
		serv = g_alloca(16);
		sprintf((gchar *) serv, "%d", is->url->port);
	} else {
		serv = "imap";
		port = "143";
	}
#ifdef HAVE_SSL
	mode = camel_url_get_param(is->url, "use_ssl");
	if (mode && strcmp(mode, "never") != 0) {
		if (!strcmp(mode, "when-possible")) {
			tcp_stream = camel_tcp_stream_ssl_new_raw(is->session, is->url->host, STARTTLS_FLAGS);
			ssl_mode = 2;
		} else {
			if (is->url->port == 0) {
				serv = "imaps";
				port = "993";
			}
			tcp_stream = camel_tcp_stream_ssl_new(is->session, is->url->host, SSL_PORT_FLAGS);
		}
		is->is_ssl_stream = TRUE;
	} else {
		tcp_stream = camel_tcp_stream_raw_new ();
		is->is_ssl_stream = FALSE;
	}
#else
	tcp_stream = camel_tcp_stream_raw_new ();
	is->is_ssl_stream = FALSE;
#endif /* HAVE_SSL */

	hints.ai_socktype = SOCK_STREAM;
	ai = camel_getaddrinfo(is->url->host, serv, &hints, ex);
	if (ex->id && ex->id != CAMEL_EXCEPTION_USER_CANCEL && port != NULL) {
		camel_exception_clear(ex);
		ai = camel_getaddrinfo(is->url->host, port, &hints, ex);
	}

	if (ex->id) {
		e(printf ("Unable to connect %d %s \n", ex->id, ex->desc));
		camel_object_unref(tcp_stream);
		return;
	}

	ret = camel_tcp_stream_connect(CAMEL_TCP_STREAM(tcp_stream), ai);
	camel_freeaddrinfo(ai);
	if (ret == -1) {
		if (errno == EINTR)
			camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL, _("Connection cancelled"));
		else
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					_("Could not connect to %s (port %s): %s"),
					is->url->host, serv, g_strerror(errno));
		camel_object_unref(tcp_stream);
		return;
	}

	is->stream = (CamelIMAPXStream *) camel_imapx_stream_new(tcp_stream);
	camel_object_unref(tcp_stream);

	/* Disable Nagle - we send a lot of small requests which nagle slows down */
	sockopt.option = CAMEL_SOCKOPT_NODELAY;
	sockopt.value.no_delay = TRUE;
	camel_tcp_stream_setsockopt((CamelTcpStream *)tcp_stream, &sockopt);

	/* Set keepalive - needed for some hosts/router configurations, we're idle a lot */
	sockopt.option = CAMEL_SOCKOPT_KEEPALIVE;
	sockopt.value.keep_alive = TRUE;
	camel_tcp_stream_setsockopt ((CamelTcpStream *)tcp_stream, &sockopt);

	camel_imapx_stream_gets (is->stream, &buffer, &len);
	e(printf("Got greeting '%.*s'\n", len, buffer));

	return;

	ic = camel_imapx_command_new("CAPABILITY", NULL, "CAPABILITY");
	imapx_command_run(is, ic, ex);
	camel_imapx_command_free(ic);

	if (camel_exception_is_set (ex))
		goto exit;

#ifdef HAVE_SSL
	if (ssl_mode == 2)
	{

		if (!(is->cinfo->capa & IMAPX_CAPABILITY_STARTTLS)) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					_("Failed to connect to IMAP server %s in secure mode: %s"),
					is->url->host, _("STARTTLS not supported"));
			goto exit;
		}

		ic = camel_imapx_command_new ("STARTTLS", NULL, "STARTTLS");
		imapx_command_run (is, ic, ex);
		camel_imapx_command_free(ic);

		if (camel_exception_is_set (ex))
			goto exit;

		if (camel_tcp_stream_ssl_enable_ssl (CAMEL_TCP_STREAM_SSL (tcp_stream)) == -1) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					_("Failed to connect to IMAP server %s in secure mode: %s"),
					is->url->host, _("SSL negotiations failed"));
			goto exit;
		}
	}
#endif

exit:
	if (camel_exception_is_set (ex)) {
		e(printf("Unable to connect %d %s \n", ex->id, ex->desc));
		camel_object_unref (is->stream);
		is->stream = NULL;

		return;
	}
}

static void
imapx_reconnect (CamelIMAPXServer *is, CamelException *ex)
{
	CamelSasl *sasl;
	CamelIMAPXCommand *ic;
retry:
	imapx_connect(is, 0, 0, ex);
	if (camel_exception_is_set (ex))
		return;

	if (is->url->passwd == NULL) {
		CamelException ex = { 0, NULL };
		gchar *prompt = g_strdup_printf(_("%sPlease enter the IMAP password for %s@%s"), "", is->url->user, is->url->host);
		const gchar *auth_domain;

		auth_domain = camel_url_get_param (is->url, "auth-domain");
		is->url->passwd = camel_session_get_password(is->session, (CamelService *)is->store,
				auth_domain,
				prompt, "password", CAMEL_SESSION_PASSWORD_SECRET, &ex);

		g_free(prompt);
		if (ex.id) {
			g_message ("Unable to connect %d ", ex.id);
			return;
		}
	}

	if (is->url->authmech
			&& (sasl = camel_sasl_new("imap", is->url->authmech, NULL))) {
		ic = camel_imapx_command_new("AUTHENTICATE", NULL, "AUTHENTICATE %A", sasl);
		camel_object_unref(sasl);
	} else {
		ic = camel_imapx_command_new("LOGIN", NULL, "LOGIN %s %s", is->url->user, is->url->passwd);
	}

	// TODO freeing data?
	imapx_command_run(is, ic, ex);
	if (camel_exception_is_set (ex)) {
		if (ic->status)
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, "Login failed: %s", ic->status->text);
		return;
	}

	camel_imapx_command_free(ic);
	if (camel_exception_is_set (ex))
		goto exception;

	/* After login we re-capa */
	if (is->cinfo) {
		imapx_free_capability(is->cinfo);
		is->cinfo = NULL;
	}

	ic = camel_imapx_command_new("CAPABILITY", NULL, "CAPABILITY");
	imapx_command_run(is, ic, ex);
	camel_imapx_command_free(ic);

	if (camel_exception_is_set (ex))
		goto exception;

	is->state = IMAPX_AUTHENTICATED;

	is->use_idle = TRUE;
	if (imapx_idle_supported (is))
		imapx_init_idle (is);

	/* Fetch namespaces */
	if (is->cinfo->capa & IMAPX_CAPABILITY_NAMESPACE) {
		ic = camel_imapx_command_new ("NAMESPACE", NULL, "NAMESPACE");
		imapx_command_run (is, ic, ex);
		camel_imapx_command_free (ic);

		if (camel_exception_is_set (ex))
			goto exception;
	}

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
		/* FIXME needs to be identified from list response */
		ns->sep = '/';
		nsl->personal = ns;
		imapx_store->summary->namespaces = nsl;
	}

	if (!camel_exception_is_set (ex))
		return;

exception:
	if (ex->id != CAMEL_EXCEPTION_USER_CANCEL) {
		c(printf("Re Connection failed: %s\n", ex->desc));
		imapx_disconnect (is);
		sleep(1);
		// camelexception_done?
		camel_exception_clear (ex);
		goto retry;
	}
}

/* ********************************************************************** */

static void
imapx_command_fetch_message_done(CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	CamelIMAPXJob *job = ic->job;
	gboolean failed = FALSE;

	/* We either have more to fetch (partial mode?), we are complete,
	   or we failed.  Failure is handled in the fetch code, so
	   we just return the job, or keep it alive with more requests */

	job->commands--;

	if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
		failed = TRUE;
		job->u.get_message.body_len = -1;
		if (job->u.get_message.stream) {
			camel_object_unref(job->u.get_message.stream);
			job->u.get_message.stream = 0;
		}
	}

	if (job->u.get_message.use_multi_fetch) {

		if (!failed && job->u.get_message.fetch_offset <= job->u.get_message.size) {
			camel_imapx_command_free (ic);
			if (job->op)
				camel_operation_progress (job->op, (job->u.get_message.fetch_offset *100)/job->u.get_message.size);

			ic = camel_imapx_command_new("FETCH", job->folder->full_name,
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
		CamelStream *stream = job->u.get_message.stream;
		/* return the exception from last command */
		if (failed) {
			if (!camel_exception_is_set (ic->ex))
				camel_exception_setv(job->ex, 1, "Error fetching message: %s", ic->status->text);
			else
				camel_exception_xfer (job->ex, ic->ex);
		} else {
			CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) job->folder;

			if (stream) {
				gchar *tmp = camel_data_cache_get_filename (ifolder->cache, "tmp", job->u.get_message.uid, NULL);

				if (camel_stream_flush (stream) == 0 && camel_stream_close (stream) == 0) {
					gchar *cache_file = camel_data_cache_get_filename  (ifolder->cache, "cur", job->u.get_message.uid, NULL);
					gchar *temp = g_strrstr (cache_file, "/"), *dir;

					dir = g_strndup (cache_file, temp - cache_file);
					g_mkdir_with_parents (dir, 0700);
					g_free (dir);

					if (link (tmp, cache_file) != 0)
						camel_exception_set (job->ex, 1, "failed to copy the tmp file");
					g_free (cache_file);
				} else 
					camel_exception_setv(job->ex, 1, "closing tmp stream failed: %s", g_strerror(errno));

				camel_data_cache_remove (ifolder->cache, "tmp", job->u.get_message.uid, NULL);
				g_free (tmp);
			}
		}

		if (stream)
			camel_object_unref (stream);
		if (job->op)
			camel_operation_end (job->op);
		imapx_job_done (is, job);
	}

	camel_imapx_command_free (ic);
}

static void
imapx_job_get_message_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;
	gint i;

	/* FIXME: MUST ensure we never try to get the same message
	 twice at the same time.

	 If this is a high-priority get, then we also
	   select the folder to make sure it runs immmediately ...

	This doesn't work yet, so we always force a select every time */

	if (job->u.get_message.use_multi_fetch) {
		for (i=0; i < 3 && job->u.get_message.fetch_offset < job->u.get_message.size;i++) {
			ic = camel_imapx_command_new("FETCH", job->folder->full_name,
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
		ic = camel_imapx_command_new("FETCH", job->folder->full_name,
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

	ic = camel_imapx_command_new ("COPY", job->folder->full_name, "UID COPY ");
	ic->complete = imapx_command_copy_messages_step_done;
	ic->job = job;
	job->u.copy_messages.last_index = i;

	for (;i < uids->len; i++) {
		gint res;
		const gchar *uid = (gchar *) g_ptr_array_index (uids, i);

		res = imapx_uidset_add (&job->u.copy_messages.uidset, ic, uid);
		if (res == 1) {
			camel_imapx_command_add (ic, " %f", job->u.copy_messages.dest->full_name);
			job->u.copy_messages.index = i;
			imapx_command_queue (is, ic);
			return;
		}
	}

	job->u.copy_messages.index = i;
	if (imapx_uidset_done (&job->u.copy_messages.uidset, ic)) {
		camel_imapx_command_add (ic, " %s", job->u.copy_messages.dest->full_name);
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

	if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
		if (!camel_exception_is_set (ic->ex))
			camel_exception_set (job->ex, 1, "Error copying messages");
		else
			camel_exception_xfer (job->ex, ic->ex);

		goto cleanup;
	}

	if (job->u.copy_messages.delete_originals) {
		gint j;

		for (j = job->u.copy_messages.last_index; j < i; j++)
			camel_folder_delete_message (job->folder, uids->pdata [j]);
	}

	/* TODO copy the summary and cached messages to the new folder. We might need a sorted insert to avoid refreshing the dest folder */
	if (ic->status->condition == IMAPX_COPYUID) {

	}

	if (i < uids->len) {
		camel_imapx_command_free (ic);
		imapx_command_copy_messages_step_start (is, job, i);
	}

cleanup:
	camel_object_unref (job->u.copy_messages.dest);
	camel_object_unref (job->folder);

	imapx_job_done (is, job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_copy_messages_start (CamelIMAPXServer *is, CamelIMAPXJob *job)
{
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

	mi = camel_message_info_clone(job->u.append_message.info);
	old_uid = g_strdup (mi->uid);

	if (!camel_exception_is_set (ic->ex) && ic->status->result == IMAPX_OK) {
		if (ic->status->condition == IMAPX_APPENDUID) {
			c(printf("Got appenduid %d %d\n", (gint)ic->status->u.appenduid.uidvalidity, (gint)ic->status->u.appenduid.uid));
			if (ic->status->u.appenduid.uidvalidity == is->uidvalidity) {
				CamelFolderChangeInfo *changes;
				char *uid;

				uid = g_strdup_printf("%u", (guint)ic->status->u.appenduid.uid);
				mi->uid = camel_pstring_add (uid, TRUE);

				cur = camel_data_cache_get_filename  (ifolder->cache, "cur", mi->uid, NULL);
				link (job->u.append_message.path, cur);

				/* should we update the message count ? */
				camel_folder_summary_add (job->folder->summary, mi);

				changes = camel_folder_change_info_new ();
				camel_folder_change_info_add_uid (changes, mi->uid);
				camel_object_trigger_event (CAMEL_OBJECT (job->folder), "folder_changed",
						changes);
				camel_folder_change_info_free (changes);

				g_free(cur);
			} else {
				g_message ("but uidvalidity changed \n");
			}
		}
	} else {
		if (!camel_exception_is_set (ic->ex))
			camel_exception_setv(job->ex, 1, "Error appending message: %s", ic->status->text);
		else
			camel_exception_xfer (job->ex, ic->ex);
	}

	if (old_uid)
		camel_data_cache_remove (ifolder->cache, "tmp", old_uid, NULL);
	g_free (old_uid);
	camel_message_info_free(job->u.append_message.info);
	g_free(job->u.append_message.path);
	camel_object_unref(job->folder);

	imapx_job_done (is, job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_append_message_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	/* TODO: we could supply the original append date from the file timestamp */
	ic = camel_imapx_command_new("APPEND", NULL,
				     "APPEND %f %F %P",
				     job->folder->full_name,
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
		if(index >= uids->len)
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
	CamelIMAPXJob *job = ic->job;
	gint i = job->u.refresh_info.index;
	GArray *infos = job->u.refresh_info.infos;

	if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
		if (!camel_exception_is_set (ic->ex))
			camel_exception_set (job->ex, 1, "Error fetching message headers");
		else
			camel_exception_xfer (job->ex, ic->ex);

		goto cleanup;
	}

	if (camel_folder_change_info_changed(job->u.refresh_info.changes)) {
		imapx_update_store_summary (job->folder);
		camel_folder_summary_save_to_db (job->folder->summary, NULL);
		camel_object_trigger_event(job->folder, "folder_changed", job->u.refresh_info.changes);
	}

	camel_folder_change_info_clear(job->u.refresh_info.changes);

	if (i<infos->len) {
		camel_imapx_command_free (ic);

		ic = camel_imapx_command_new("FETCH", job->folder->full_name, "UID FETCH ");
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

cleanup:
	if (job->op)
		camel_operation_end (job->op);

	for (i=0;i<infos->len;i++) {
		struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

		g_free(r->uid);
	}
	g_array_free(job->u.refresh_info.infos, TRUE);

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

	if (!camel_exception_is_set (ic->ex) && ic->status->result == IMAPX_OK) {
		GCompareDataFunc uid_cmp = imapx_uid_cmp;
		CamelMessageInfo *s_minfo = NULL;
		CamelIMAPXMessageInfo *info;
		CamelFolderSummary *s = job->folder->summary;
		GSList *removed = NULL, *l;
		gboolean fetch_new = FALSE;
		gint i;
		guint j = 0;
		GPtrArray *uids;

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

				if (imapx_update_message_info_flags ((CamelMessageInfo *) info, r->server_flags, r->server_user_flags, job->folder))
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

			mi = camel_folder_summary_uid (is->select_folder->summary, uid);
			if (mi) {
				imapx_update_summary_for_removed_message (mi, is->select_folder);
				camel_message_info_free (mi);
			}

			camel_folder_change_info_remove_uid (job->u.refresh_info.changes, uid);
			camel_folder_summary_remove_uid_fast (s, uid);
		}

		if (removed) {
			camel_db_delete_uids (is->store->cdb_w, s->folder->full_name, removed, NULL);
			g_slist_foreach (removed, (GFunc) g_free, NULL);
			g_slist_free (removed);
		}
		
		imapx_update_store_summary (job->folder);

		if (camel_folder_change_info_changed(job->u.refresh_info.changes))
			camel_object_trigger_event(job->folder, "folder_changed", job->u.refresh_info.changes);
		camel_folder_change_info_clear(job->u.refresh_info.changes);

		camel_folder_free_uids (job->folder, uids);

		/* If we have any new messages, download their headers, but only a few (100?) at a time */
		if (fetch_new) {
			camel_operation_start (job->op, _("Fetching summary information for new messages in %s"), job->folder->name);
			imapx_uidset_init(&job->u.refresh_info.uidset, BATCH_FETCH_COUNT, 0);
			/* command will be free'ed in step_fetch_done */
			imapx_command_step_fetch_done(is, ic);
			return;
		}
	} else {
		if (!camel_exception_is_set (ic->ex))
			camel_exception_setv(job->ex, 1, "Error retriving message: %s", ic->status->text);
		else
			camel_exception_xfer (job->ex, ic->ex);
	}

	for (i=0;i<infos->len;i++) {
		struct _refresh_info *r = &g_array_index(infos, struct _refresh_info, i);

		g_free(r->uid);
	}

	if (job->op)
		camel_operation_end (job->op);
	g_array_free(job->u.refresh_info.infos, TRUE);
	imapx_job_done (is, job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_scan_changes_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	camel_operation_start (job->op, _("Scanning for changed messages in %s"), job->folder->name);

	ic = camel_imapx_command_new ("FETCH", job->folder->full_name,
				     "FETCH 1:* (UID FLAGS)");
	ic->job = job;
	ic->complete = imapx_job_scan_changes_done;
	job->u.refresh_info.infos = g_array_new (0, 0, sizeof(struct _refresh_info));
	imapx_command_queue (is, ic);
}

static void
imapx_command_fetch_new_messages_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
		if (!camel_exception_is_set (ic->ex))
			camel_exception_setv(ic->job->ex, 1, "Error fetching new messages : %s", ic->status->text);
		else
			camel_exception_xfer (ic->job->ex, ic->ex);
		goto exception;
	}

	if (camel_folder_change_info_changed(ic->job->u.refresh_info.changes)) {
		imapx_update_store_summary (ic->job->folder);
		camel_folder_summary_save_to_db (ic->job->folder->summary, NULL);
		camel_object_trigger_event(ic->job->folder, "folder_changed", ic->job->u.refresh_info.changes);
		camel_folder_change_info_clear(ic->job->u.refresh_info.changes);
	}


exception:
	if (ic->job->noreply)
		camel_folder_change_info_free(ic->job->u.refresh_info.changes);

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

	if (total > 0)
		uid = camel_folder_summary_uid_from_index (folder->summary, total - 1);
	else
		uid = g_strdup ("1");

	camel_operation_start (job->op, _("Fetching summary information for new messages in %s"), folder->name);

	if (diff > BATCH_FETCH_COUNT) {
		ic = camel_imapx_command_new ("FETCH", job->folder->full_name,
				     "FETCH %s:* (UID FLAGS)", uid);
		imapx_uidset_init(&job->u.refresh_info.uidset, BATCH_FETCH_COUNT, 0);
		job->u.refresh_info.infos = g_array_new (0, 0, sizeof(struct _refresh_info));
		ic->complete = imapx_command_step_fetch_done;
	} else {
		ic = camel_imapx_command_new ("FETCH", job->folder->full_name,
					"UID FETCH %s:* (RFC822.SIZE RFC822.HEADER FLAGS)", uid);
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
	CamelFolder *folder = job->folder;
	CamelException *ex = job->ex;
	
	total = camel_folder_summary_count (folder->summary);
	/* Check if there are any new messages. The old imap doc says one needs to reselect in case of inbox to fetch
	   new messages. Need to check if its still true. Just use noop now */
	if (ifolder->exists_on_server == total) {
		camel_imapx_server_noop (is, folder, ex);

		if (camel_exception_is_set (ex))
			goto exception;
	}

	/* Fetch the new messages */
	if (ifolder->exists_on_server > total)
	{
		imapx_server_fetch_new_messages (is, folder, FALSE, job->ex);
		if (camel_exception_is_set (job->ex))
			goto exception;
	}

	/* Sync changes before fetching status, else unread count will not match. need to think about better ways for this */
	camel_imapx_server_sync_changes (is, folder, ex);
	if (camel_exception_is_set (job->ex))
		goto exception;

	/* Check if a rescan is needed */
	total = camel_folder_summary_count (folder->summary);
	if (ifolder->exists_on_server == total) {
		guint32 unread;
		CamelIMAPXCommand *ic;

		ic = camel_imapx_command_new ("STATUS", folder->full_name, "STATUS %s (MESSAGES UNSEEN)", folder->full_name);
		ic->job = job;
		ic->complete = imapx_command_status_done;
		imapx_command_run_sync (is, ic);

		if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
			if (!camel_exception_is_set (ic->ex))
				camel_exception_setv(job->ex, 1, "Error refreshing folder: %s", ic->status->text);
			else
				camel_exception_xfer (job->ex, ic->ex);

			camel_imapx_command_free (ic);
			goto exception;
		}
		camel_imapx_command_free (ic);

		camel_object_get (folder, NULL, CAMEL_FOLDER_UNREAD, &unread, NULL);
		if (ifolder->exists_on_server == total && unread == ifolder->unread_on_server)
			goto exception;
	}

	imapx_job_scan_changes_start (is, job);
	return;

exception:
	imapx_job_done (is, job);
}


/* ********************************************************************** */

static void
imapx_command_expunge_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
		if (!camel_exception_is_set (ic->ex))
			camel_exception_setv(ic->job->ex, 1, "Error expunging message : %s", ic->status->text);
		else
			camel_exception_xfer (ic->job->ex, ic->ex);
	} else {
		GPtrArray *uids;
		CamelFolder *folder = ic->job->folder;
			
		camel_folder_summary_save_to_db (folder->summary, ic->job->ex);
		uids = camel_db_get_folder_deleted_uids (folder->parent_store->cdb_r, folder->full_name, ic->job->ex);
		
		if (uids && uids->len)	{
			CamelFolderChangeInfo *changes;
			GSList *removed = NULL;
			gint i;

			changes = camel_folder_change_info_new ();
			for (i = 0; i < uids->len; i++) {
				gchar *uid = uids->pdata [i];
				CamelMessageInfo *mi = camel_folder_summary_uid (folder->summary, uid);

				if (mi) {
					imapx_update_summary_for_removed_message (mi, folder);
					camel_message_info_free (mi);
				}

				camel_folder_summary_remove_uid_fast (folder->summary, uid);
				camel_folder_change_info_remove_uid (changes, uids->pdata[i]);
				removed = g_slist_prepend (removed, (gpointer) uids->pdata[i]);
			}

			camel_db_delete_uids (folder->parent_store->cdb_w, folder->full_name, removed, ic->job->ex);
			camel_folder_summary_save_to_db (folder->summary, ic->job->ex);
			camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", changes);
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

	/* TODO handle UIDPLUS capability */
	ic = camel_imapx_command_new("EXPUNGE", job->folder->full_name, "EXPUNGE");
	ic->job = job;
	ic->complete = imapx_command_expunge_done;
	imapx_command_queue(is, ic);
}

/* ********************************************************************** */

static void
imapx_command_list_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
		if (!camel_exception_is_set (ic->ex))
			camel_exception_setv(ic->job->ex, 1, "Error fetching folders : %s", ic->status->text);
		else
			camel_exception_xfer (ic->job->ex, ic->ex);
	}

	e(printf ("==== list or lsub completed ==== \n"));
	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_list_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	ic = camel_imapx_command_new("LIST", NULL, "%s \"\" %s",
				     (job->u.list.flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)?"LSUB":"LIST",
				     job->u.list.pattern);
	ic->pri = job->pri;
	ic->job = job;
	ic->complete = imapx_command_list_done;
	imapx_command_queue(is, ic);
}

static void
imapx_command_noop_done (CamelIMAPXServer *is, CamelIMAPXCommand *ic)
{
	if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
		if (!camel_exception_is_set (ic->ex))
			camel_exception_setv(ic->job->ex, 1, "Error performing NOOP: %s", ic->status->text);
		else
			camel_exception_xfer (ic->job->ex, ic->ex);
	}

	imapx_job_done (is, ic->job);
	camel_imapx_command_free (ic);
}

static void
imapx_job_noop_start(CamelIMAPXServer *is, CamelIMAPXJob *job)
{
	CamelIMAPXCommand *ic;

	if (job->folder)
		ic = camel_imapx_command_new ("NOOP", job->folder->full_name, "NOOP");
	else
		ic = camel_imapx_command_new ("NOOP", NULL, "NOOP");
	
	ic->job = job;
	ic->complete = imapx_command_noop_done;
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
	{ "\\RECENT", CAMEL_IMAPX_MESSAGE_RECENT }
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
	gboolean failed = FALSE;

	job->commands--;

	/* If this worked, we should really just update the changes that we sucessfully
	   stored, so we dont have to worry about sending them again ...
	   But then we'd have to track which uid's we actually updated, so its easier
	   just to refresh all of the ones we got.

	   Not that ... given all the asynchronicity going on, we're guaranteed
	   that what we just set is actually what is on the server now .. but
	   if it isn't, i guess we'll fix up next refresh */

	if (camel_exception_is_set (ic->ex) || ic->status->result != IMAPX_OK) {
		if (!camel_exception_is_set (ic->ex))
			camel_exception_setv(job->ex, 1, "Error syncing changes: %s", ic->status->text);
		else
			camel_exception_xfer (job->ex, ic->ex);
		failed = TRUE;
	}

	/* lock cache ? */
	if (!failed)
	{
		gint i;

		for (i=0;i<job->u.sync_changes.changed_uids->len;i++) {
			CamelIMAPXMessageInfo *info = (CamelIMAPXMessageInfo *) camel_folder_summary_uid (job->folder->summary,
					job->u.sync_changes.changed_uids->pdata[i]);

			if (!info)
				continue;
			
			info->server_flags = ((CamelMessageInfoBase *)info)->flags & CAMEL_IMAPX_SERVER_FLAGS;
			info->info.flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
			info->info.dirty = TRUE;

			camel_folder_summary_touch (job->folder->summary);
			camel_message_info_free (info);

			/* FIXME: move over user flags too */
		}
	}

	if (job->commands == 0) {
		if (job->folder->summary && (job->folder->summary->flags & CAMEL_SUMMARY_DIRTY) != 0) {
			CamelStoreInfo *si;

			/* ... and store's summary when folder's summary is dirty */
			si = camel_store_summary_path ((CamelStoreSummary *)((CamelIMAPXStore *) job->folder->parent_store)->summary, job->folder->full_name);
			if (si) {
				if (si->total != job->folder->summary->saved_count || si->unread != job->folder->summary->unread_count) {
					si->total = job->folder->summary->saved_count;
					si->unread = job->folder->summary->unread_count;
					camel_store_summary_touch ((CamelStoreSummary *)((CamelIMAPXStore *) job->folder->parent_store)->summary);
				}

				camel_store_summary_info_free ((CamelStoreSummary *)((CamelIMAPXStore *) job->folder->parent_store)->summary, si);
			}
		}

		camel_folder_summary_save_to_db (job->folder->summary, job->ex);
		camel_store_summary_save((CamelStoreSummary *)((CamelIMAPXStore *) job->folder->parent_store)->summary);

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
						ic = camel_imapx_command_new("STORE", job->folder->full_name, "UID STORE ");
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
						ic = camel_imapx_command_new("STORE", job->folder->full_name, "UID STORE ");
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
cancel_all_jobs (CamelIMAPXServer *is, CamelException *ex)
{
	CamelIMAPXCommand *cw, *cn;
	gint i = 0;

	while (i < 2) {
		QUEUE_LOCK(is);
		if (i == 1)
			cw = (CamelIMAPXCommand *) is->queue.head;
		else
			cw = (CamelIMAPXCommand *) is->active.head;

		cn = cw->next;
		QUEUE_UNLOCK(is);

		while (cn) {
			QUEUE_LOCK(is);
			camel_dlist_remove ((CamelDListNode *)cw);
			QUEUE_UNLOCK(is);

			camel_exception_set (cw->ex, ex->id, ex->desc);

			cw->complete (is, cw);
			cw = cn;

			QUEUE_LOCK(is);
			cn = cn->next;
			QUEUE_UNLOCK(is);
		}

		i++;
	}
}

/* ********************************************************************** */

static void
parse_contents (CamelIMAPXServer *is, CamelException *ex)
{
	gint buffered = 0;

	do {
		imapx_step(is, ex);
		
		g_static_rec_mutex_lock (&is->istream_lock);
		
		buffered = camel_imapx_stream_buffered (is->stream);

		g_static_rec_mutex_unlock (&is->istream_lock);

	} while (buffered && !camel_exception_is_set (ex));
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
	CamelException ex = CAMEL_EXCEPTION_INITIALISER;
	CamelOperation *op;

	op = camel_operation_new (NULL, NULL);
	op = camel_operation_register (op);
	is->op = op;

	while (!camel_exception_is_set (&ex) && is->stream) {
		camel_operation_uncancel (op);
#ifdef HAVE_SSL
		if (is->is_ssl_stream)	{
			PRPollDesc pollfds[2] = { };
			gint res;

			g_static_rec_mutex_lock (&is->istream_lock);

			pollfds[0].fd = camel_tcp_stream_ssl_sockfd ((CamelTcpStreamSSL *)is->stream->source);
			pollfds[0].in_flags = PR_POLL_READ;
			pollfds[1].fd = camel_operation_cancel_prfd (op);
			pollfds[1].in_flags = PR_POLL_READ;

			g_static_rec_mutex_unlock (&is->istream_lock);
#include <prio.h>

			res = PR_Poll(pollfds, 2, PR_MillisecondsToInterval (30 * 1000));
			if (res == -1)
				sleep(1) /* ?? */ ;
			else if (res == 0) {
				/* timed out */
			} else if ((pollfds[0].out_flags & PR_POLL_READ)) {
				parse_contents (is, &ex);
			} else if (pollfds[1].out_flags & PR_POLL_READ)
				errno = EINTR;
		}
#endif

		if (!is->is_ssl_stream)	{
			struct pollfd fds[2] = { {0, 0, 0}, {0, 0, 0} };
			gint res;

			g_static_rec_mutex_lock (&is->istream_lock);
			
			fds[0].fd = ((CamelTcpStreamRaw *)is->stream->source)->sockfd;
			fds[0].events = POLLIN;
			fds[1].fd = camel_operation_cancel_fd (op);
			fds[1].events = POLLIN;
			
			g_static_rec_mutex_unlock (&is->istream_lock);

			res = poll(fds, 2, 1000*30);
			if (res == -1)
				sleep(1) /* ?? */ ;
			else if (res == 0)
				/* timed out */;
			else if (fds[0].revents & POLLIN) {
				parse_contents (is, &ex);
			} else if (fds[1].revents & POLLIN)
				errno = EINTR;
		}
	
		if (camel_application_is_exiting || is->parser_quit) {
			camel_exception_setv (&ex, CAMEL_EXCEPTION_USER_CANCEL, "Operation Cancelled: %s", g_strerror(errno));
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
				camel_exception_setv (&ex, CAMEL_EXCEPTION_USER_CANCEL, "Operation Cancelled: %s", g_strerror(errno));
		}
	}

	imapx_disconnect (is);
	cancel_all_jobs (is, &ex);

	if (imapx_idle_supported (is))
		imapx_exit_idle (is);

	camel_exception_clear (&ex);

	if (op)
		camel_operation_unref (op);
	is->op = NULL;

	is->parser_thread = NULL;
	is->parser_quit = FALSE;
	return NULL;
}

static void
imapx_server_class_init(CamelIMAPXServerClass *ieclass)
{
	ieclass->tagprefix = 'A';

//	camel_object_class_add_event((CamelObjectClass *)ieclass, "status", NULL);
}

static void
imapx_server_init(CamelIMAPXServer *is, CamelIMAPXServerClass *isclass)
{
	camel_dlist_init(&is->queue);
	camel_dlist_init(&is->active);
	camel_dlist_init(&is->done);
	camel_dlist_init(&is->jobs);

	/* not used at the moment. Use it in future */
	is->job_timeout = 29 * 60 * 1000 * 1000;

	g_static_rec_mutex_init (&is->queue_lock);
	g_static_rec_mutex_init (&is->ostream_lock);
	g_static_rec_mutex_init (&is->istream_lock);

	is->tagprefix = isclass->tagprefix;
	isclass->tagprefix++;
	if (isclass->tagprefix > 'Z')
		isclass->tagprefix = 'A';
	is->tagprefix = 'A';

	is->state = IMAPX_DISCONNECTED;

	is->expunged = NULL;
	is->changes = camel_folder_change_info_new ();
	is->parser_quit = FALSE;
}

static void
imapx_server_finalise(CamelIMAPXServer *is, CamelIMAPXServerClass *isclass)
{
	g_static_rec_mutex_free(&is->queue_lock);
	g_static_rec_mutex_free (&is->ostream_lock);
	g_static_rec_mutex_free (&is->istream_lock);

	camel_folder_change_info_free (is->changes);
}

CamelType
camel_imapx_server_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (
			camel_object_get_type (),
			"CamelIMAPXServer",
			sizeof (CamelIMAPXServer),
			sizeof (CamelIMAPXServerClass),
			(CamelObjectClassInitFunc) imapx_server_class_init,
			NULL,
			(CamelObjectInitFunc) imapx_server_init,
			(CamelObjectFinalizeFunc) imapx_server_finalise);
	}

	return type;
}

CamelIMAPXServer *
camel_imapx_server_new(CamelStore *store, CamelURL *url)
{
	CamelIMAPXServer *is = (CamelIMAPXServer *)camel_object_new(camel_imapx_server_get_type());

	is->session = ((CamelService *)store)->session;
	camel_object_ref(is->session);
	is->store = store;
	is->url = camel_url_copy(url);

	return is;
}

static gboolean
imapx_disconnect (CamelIMAPXServer *is)
{
	gboolean ret = TRUE;

	g_static_rec_mutex_lock (&is->istream_lock);
	g_static_rec_mutex_lock (&is->ostream_lock);

	if (is->state == IMAPX_DISCONNECTED)
		goto exit;

	if (is->stream) {
		if (camel_stream_close (is->stream->source) == -1)
			ret = FALSE;

		camel_object_unref (CAMEL_OBJECT (is->stream));
		is->stream = NULL;
	}

	/* TODO need a select lock */
	if (is->select_folder) {
		camel_object_unref(is->select_folder);
		is->select_folder = NULL;
	}

	if (is->select) {
		g_free(is->select);
		is->select = NULL;
	}

	if (is->select_pending) {
		camel_object_unref(is->select_pending);
		is->select_pending = NULL;
	}

	if (is->literal) {
		camel_imapx_command_free (is->literal);
		is->literal = NULL;
	}
	
	is->state = IMAPX_DISCONNECTED;

exit:	
	g_static_rec_mutex_unlock (&is->ostream_lock);
	g_static_rec_mutex_unlock (&is->istream_lock);

	return ret;
}

/* Client commands */
gboolean
camel_imapx_server_connect(CamelIMAPXServer *is, gint state)
{
	gboolean ret = FALSE;

	g_static_rec_mutex_lock (&is->istream_lock);
	if (state) {
		CamelException ex = CAMEL_EXCEPTION_INITIALISER;

		if (is->state == IMAPX_AUTHENTICATED || is->state == IMAPX_SELECTED) {
			ret = TRUE;
			goto exit;
		}

		g_static_rec_mutex_lock (&is->ostream_lock);
		imapx_reconnect (is, &ex);
		g_static_rec_mutex_unlock (&is->ostream_lock);
		if (camel_exception_is_set (&ex)) {
			ret = FALSE;
			goto exit;
		}

		is->parser_thread = g_thread_create((GThreadFunc) imapx_parser_thread, is, TRUE, NULL);
		ret = TRUE;
	} else {
		g_static_rec_mutex_unlock (&is->istream_lock);

		is->parser_quit = TRUE;
		camel_operation_cancel (is->op);
		if (is->parser_thread)
			g_thread_join (is->parser_thread);
		
		return TRUE;
	}

exit:
	g_static_rec_mutex_unlock (&is->istream_lock);
	return ret;
}

static CamelStream *
imapx_server_get_message (CamelIMAPXServer *is, CamelFolder *folder, CamelOperation *op, const gchar *uid, gint pri, CamelException *ex)
{
	CamelStream *stream = NULL, *tmp_stream;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelIMAPXJob *job;
	gchar *cache_file = NULL;
	CamelMessageInfo *mi;
	gboolean registered;

	cache_file = camel_data_cache_get_filename  (ifolder->cache, "cur", uid, NULL);
	if (g_file_test (cache_file, G_FILE_TEST_EXISTS)) {
		g_free (cache_file);
		return NULL;
	}
	g_free (cache_file);

	QUEUE_LOCK (is);
	
	if (imapx_is_job_in_queue (is, folder->full_name, IMAPX_JOB_GET_MESSAGE, uid)) {
		/* TODO set a right exception */
		camel_exception_set (ex, CAMEL_EXCEPTION_OPERATION_IN_PROGRESS, "Downloading message...");
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
	job->ex = ex;

	mi = camel_folder_summary_uid (folder->summary, uid);
	if (((CamelMessageInfoBase *) mi)->size > MULTI_SIZE)
		job->u.get_message.use_multi_fetch = TRUE;

	job->u.get_message.size = ((CamelMessageInfoBase *) mi)->size;
	camel_message_info_free (mi);
	registered = imapx_register_job (is, job);
	
	QUEUE_UNLOCK (is);

	if (registered) {
		imapx_run_job(is, job);
		stream = camel_data_cache_get (ifolder->cache, "cur", uid, NULL);
	}

	g_free(job);

	return stream;
}

CamelStream *
camel_imapx_server_get_message(CamelIMAPXServer *is, CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelStream *stream;
	CamelOperation *op = camel_operation_registered ();
	
	stream = imapx_server_get_message(is, folder, op, uid, 100, ex);
	if (op)
		camel_operation_unref (op);

	return stream;
}

void
camel_imapx_server_sync_message (CamelIMAPXServer *is, CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	gchar *cache_file = NULL;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelStream *stream;

	cache_file = camel_data_cache_get_filename  (ifolder->cache, "cur", uid, NULL);
	if (g_file_test (cache_file, G_FILE_TEST_EXISTS)) {
		g_free (cache_file);
		return;
	}

	stream = imapx_server_get_message (is, folder, NULL, uid, 10, ex);
	if (stream)
		camel_object_unref(stream);
}

void
camel_imapx_server_copy_message (CamelIMAPXServer *is, CamelFolder *source, CamelFolder *dest, GPtrArray *uids, gboolean delete_originals, CamelException *ex)
{
	CamelIMAPXJob *job;

	job = g_malloc0(sizeof(*job));
	job->pri = -60;
	job->type = IMAPX_JOB_COPY_MESSAGE;
	job->start = imapx_job_copy_messages_start;
	job->folder = source;
	job->u.copy_messages.dest = dest;
	job->u.copy_messages.uids = uids;
	job->u.copy_messages.delete_originals = delete_originals;

	camel_object_ref(source);
	camel_object_ref (dest);

	if (imapx_register_job (is, job))
		imapx_run_job (is, job);
}

void
camel_imapx_server_append_message(CamelIMAPXServer *is, CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *mi, CamelException *ex)
{
	gchar *uid = NULL, *tmp = NULL;
	CamelStream *stream, *filter;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelMimeFilter *canon;
	CamelIMAPXJob *job;
	CamelMessageInfo *info;
	gint res;

	/* Append just assumes we have no/a dodgy connection.  We dump stuff into the 'new'
	   directory, and let the summary know it's there.  Then we fire off a no-reply
	   job which will asynchronously upload the message at some point in the future,
	   and fix up the summary to match */

	/* chen cleanup this later */
	uid = imapx_get_temp_uid ();
	stream = camel_data_cache_add (ifolder->cache, "tmp", uid, NULL);
	if (stream == NULL) {
		camel_exception_setv(ex, 2, "Cannot create spool file: %s", g_strerror((gint) errno));
		goto fail;
	}

	filter = (CamelStream *)camel_stream_filter_new_with_stream(stream);
	camel_object_unref(stream);
	canon = camel_mime_filter_canon_new(CAMEL_MIME_FILTER_CANON_CRLF);
	camel_stream_filter_add((CamelStreamFilter *)filter, canon);
	res = camel_data_wrapper_write_to_stream((CamelDataWrapper *)message, filter);
	camel_object_unref(canon);
	camel_object_unref(filter);

	if (res == -1) {
		camel_exception_setv(ex, 2, "Cannot create spool file: %s", g_strerror(errno));
		goto fail;
	}

	tmp = camel_data_cache_get_filename (ifolder->cache, "tmp", uid, NULL);
	info = camel_folder_summary_info_new_from_message((CamelFolderSummary *)folder->summary, message, NULL);
	info->uid = uid;
	((CamelMessageInfoBase *) info)->flags = ((CamelMessageInfoBase *) mi)->flags;
	uid = NULL;

	// FIXME

	/* So, we actually just want to let the server loop that
	   messages need appending, i think.  This is so the same
	   mechanism is used for normal uploading as well as
	   offline re-syncing when we go back online */

	job = g_malloc0(sizeof(*job));
	job->pri = -60;
	job->type = IMAPX_JOB_APPEND_MESSAGE;
	job->noreply = 1;
	job->start = imapx_job_append_message_start;
	job->folder = folder;
	camel_object_ref(folder);
	job->u.append_message.info = info;
	job->u.append_message.path = tmp;

	if (imapx_register_job (is, job))
		imapx_run_job(is, job);
fail:
	return;
}

void
camel_imapx_server_noop (CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex)
{
	CamelIMAPXJob *job;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_NOOP;
	job->start = imapx_job_noop_start;
	job->folder = folder;
	job->ex = ex;

	if (imapx_register_job (is, job))
		imapx_run_job(is, job);

	g_free(job);
}

void
camel_imapx_server_refresh_info (CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex)
{
	CamelIMAPXJob *job;
	gboolean registered = TRUE;

	QUEUE_LOCK (is);

	if (imapx_is_job_in_queue (is, folder->full_name, IMAPX_JOB_REFRESH_INFO, NULL)) {
		QUEUE_UNLOCK (is);
		return;
	}

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_REFRESH_INFO;
	job->start = imapx_job_refresh_info_start;
	job->folder = folder;
	job->ex = ex;
	job->op = camel_operation_registered ();
	job->u.refresh_info.changes = camel_folder_change_info_new();

	if (g_ascii_strcasecmp(folder->full_name, "INBOX") == 0)
		job->pri += 10;

	registered = imapx_register_job (is, job);

	QUEUE_UNLOCK (is);

	if (registered) {
		imapx_run_job (is, job);
	
		if (camel_folder_change_info_changed(job->u.refresh_info.changes))
			camel_object_trigger_event(folder, "folder_changed", job->u.refresh_info.changes);
	}

	camel_folder_change_info_free(job->u.refresh_info.changes);
	
	if (job->op)
		camel_operation_unref (job->op);
	g_free(job);
}

static void
imapx_sync_free_user(GArray *user_set)
{
	gint i;

	if (user_set == NULL)
		return;

	for (i=0;i<user_set->len;i++) {
		GPtrArray *infos = g_array_index (user_set, struct _imapx_flag_change, i).infos;
		gint j;
		
		for (j = 0; j < infos->len; j++) {
			CamelMessageInfo *info = g_ptr_array_index (infos, j);
			camel_message_info_free (info);
		}
		
		g_ptr_array_free(infos, TRUE);
	}
	g_array_free(user_set, TRUE);
}

void
camel_imapx_server_sync_changes(CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex)
{
	guint i, on_orset, off_orset;
	GPtrArray *uids;
	GArray *on_user = NULL, *off_user = NULL;
	CamelIMAPXMessageInfo *info;
	CamelIMAPXJob *job;
	gboolean registered;

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
		g_ptr_array_free (uids, TRUE);
		return;
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
				struct _imapx_flag_change *change = NULL, add;

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

	if ((on_orset|off_orset) == 0 && on_user == NULL && off_user == NULL)
		return;

	/* TODO above code should go into changes_start */

	QUEUE_LOCK (is);

	if (imapx_is_job_in_queue (is, folder->full_name, IMAPX_JOB_SYNC_CHANGES, NULL)) {
		QUEUE_UNLOCK (is);
		goto done;
	}

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_SYNC_CHANGES;
	job->start = imapx_job_sync_changes_start;
	job->pri = -50;
	job->folder = folder;
	job->ex = ex;
	job->u.sync_changes.changed_uids = uids;
	job->u.sync_changes.on_set = on_orset;
	job->u.sync_changes.off_set = off_orset;
	job->u.sync_changes.on_user = on_user;
	job->u.sync_changes.off_user = off_user;

	registered = imapx_register_job (is, job);
	
	QUEUE_UNLOCK (is);

	if (registered)
		imapx_run_job(is, job);

	g_free(job);

done:
	imapx_sync_free_user(on_user);
	imapx_sync_free_user(off_user);

	camel_folder_free_uids (folder, uids);
}

/* expunge-uids? */
void
camel_imapx_server_expunge(CamelIMAPXServer *is, CamelFolder *folder, CamelException *ex)
{
	CamelIMAPXJob *job;
	gboolean registered;

	/* Do we really care to wait for this one to finish? */
	QUEUE_LOCK (is);

	if (imapx_is_job_in_queue (is, folder->full_name, IMAPX_JOB_EXPUNGE, NULL)) {
		QUEUE_UNLOCK (is);
		return;
	}

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_EXPUNGE;
	job->start = imapx_job_expunge_start;
	job->pri = -120;
	job->folder = folder;
	job->ex = ex;
	
	registered = imapx_register_job (is, job);
	
	QUEUE_UNLOCK (is);

	if (registered)
		imapx_run_job(is, job);

	g_free(job);
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
camel_imapx_server_list(CamelIMAPXServer *is, const gchar *top, guint32 flags, CamelException *ex)
{
	CamelIMAPXJob *job;
	GPtrArray *folders = NULL;

	job = g_malloc0(sizeof(*job));
	job->type = IMAPX_JOB_LIST;
	job->start = imapx_job_list_start;
	job->pri = -80;
	job->ex = ex;
	job->u.list.flags = flags;
	job->u.list.folders = g_hash_table_new(imapx_name_hash, imapx_name_equal);
	job->u.list.pattern = g_alloca(strlen(top)+5);
	if (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)
		sprintf(job->u.list.pattern, "%s*", top);
	else
		sprintf(job->u.list.pattern, "%s", top);

	if (imapx_register_job (is, job)) {
		imapx_run_job(is, job);

		folders = g_ptr_array_new();
		g_hash_table_foreach(job->u.list.folders, imapx_list_flatten, folders);
		qsort(folders->pdata, folders->len, sizeof(folders->pdata[0]), imapx_list_cmp);
	}
	
	g_hash_table_destroy(job->u.list.folders);
	g_free(job);

	return folders;
}

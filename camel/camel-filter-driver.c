/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#ifndef G_OS_WIN32
#include <sys/wait.h>
#endif

#include <libedataserver/e-sexp.h>

#include "camel-debug.h"
#include "camel-file-utils.h"
#include "camel-filter-driver.h"
#include "camel-filter-search.h"
#include "camel-list-utils.h"
#include "camel-mime-message.h"
#include "camel-service.h"
#include "camel-session.h"
#include "camel-stream-fs.h"
#include "camel-stream-mem.h"

#define d(x)

/* an invalid pointer */
#define FOLDER_INVALID ((gpointer)~0)

#define CAMEL_FILTER_DRIVER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_FILTER_DRIVER, CamelFilterDriverPrivate))

/* type of status for a log report */
enum filter_log_t {
	FILTER_LOG_NONE,
	FILTER_LOG_START,       /* start of new log entry */
	FILTER_LOG_ACTION,      /* an action performed */
	FILTER_LOG_END          /* end of log */
};

/* list of rule nodes */
struct _filter_rule {
	struct _filter_rule *next;
	struct _filter_rule *prev;

	gchar *match;
	gchar *action;
	gchar *name;
};

struct _CamelFilterDriverPrivate {
	GHashTable *globals;       /* global variables */

	CamelSession *session;

	CamelFolder *defaultfolder;        /* defualt folder */

	CamelFilterStatusFunc *statusfunc; /* status callback */
	gpointer statusdata;                  /* status callback data */

	CamelFilterShellFunc *shellfunc;    /* execute shell command callback */
	gpointer shelldata;                    /* execute shell command callback data */

	CamelFilterPlaySoundFunc *playfunc; /* play-sound command callback */
	gpointer playdata;                     /* play-sound command callback data */

	CamelFilterSystemBeepFunc *beep;    /* system beep callback */
	gpointer beepdata;                     /* system beep callback data */

	/* for callback */
	CamelFilterGetFolderFunc get_folder;
	gpointer data;

	/* run-time data */
	GHashTable *folders;       /* folders that message has been copied to */
	gint closed;		   /* close count */
	GHashTable *only_once;     /* actions to run only-once */

	gboolean terminated;       /* message processing was terminated */
	gboolean deleted;          /* message was marked for deletion */
	gboolean copied;           /* message was copied to some folder or another */
	gboolean moved;		   /* message was moved to some folder or another */

	CamelMimeMessage *message; /* input message */
	CamelMessageInfo *info;    /* message summary info */
	const gchar *uid;           /* message uid */
	CamelFolder *source;       /* message source folder */
	gboolean modified;         /* has the input message been modified? */

	FILE *logfile;             /* log file */

	CamelDList rules;		   /* list of _filter_rule structs */

	GError *error;

	/* evaluator */
	ESExp *eval;
};

static void camel_filter_driver_log (CamelFilterDriver *driver, enum filter_log_t status, const gchar *desc, ...);

static CamelFolder *open_folder (CamelFilterDriver *d, const gchar *folder_url);
static gint close_folders (CamelFilterDriver *d);

static ESExpResult *do_delete (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_forward_to (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_copy (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_move (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_stop (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_label (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_color (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_score (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_adjust_score(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *set_flag (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *unset_flag (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_shell (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_beep (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *play_sound (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *do_only_once (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);
static ESExpResult *pipe_message (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *);

/* these are our filter actions - each must have a callback */
static struct {
	const gchar *name;
	ESExpFunc *func;
	gint type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} symbols[] = {
	{ "delete",            (ESExpFunc *) do_delete,    0 },
	{ "forward-to",        (ESExpFunc *) do_forward_to, 0 },
	{ "copy-to",           (ESExpFunc *) do_copy,      0 },
	{ "move-to",           (ESExpFunc *) do_move,      0 },
	{ "stop",              (ESExpFunc *) do_stop,      0 },
	{ "set-label",         (ESExpFunc *) do_label,     0 },
	{ "set-color",        (ESExpFunc *) do_color,    0 },
	{ "set-score",         (ESExpFunc *) do_score,     0 },
	{ "adjust-score",      (ESExpFunc *) do_adjust_score, 0 },
	{ "set-system-flag",   (ESExpFunc *) set_flag,     0 },
	{ "unset-system-flag", (ESExpFunc *) unset_flag,   0 },
	{ "pipe-message",      (ESExpFunc *) pipe_message, 0 },
	{ "shell",             (ESExpFunc *) do_shell,     0 },
	{ "beep",              (ESExpFunc *) do_beep,      0 },
	{ "play-sound",        (ESExpFunc *) play_sound,   0 },
	{ "only-once",         (ESExpFunc *) do_only_once, 0 }
};

G_DEFINE_TYPE (CamelFilterDriver, camel_filter_driver, CAMEL_TYPE_OBJECT)

static void
free_hash_strings (gpointer key, gpointer value, gpointer data)
{
	g_free (key);
	g_free (value);
}

static void
filter_driver_dispose (GObject *object)
{
	CamelFilterDriverPrivate *priv;

	priv = CAMEL_FILTER_DRIVER_GET_PRIVATE (object);

	if (priv->defaultfolder != NULL) {
		camel_folder_thaw (priv->defaultfolder);
		g_object_unref (priv->defaultfolder);
		priv->defaultfolder = NULL;
	}

	if (priv->session != NULL) {
		g_object_unref (priv->session);
		priv->session = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_filter_driver_parent_class)->dispose (object);
}

static void
filter_driver_finalize (GObject *object)
{
	CamelFilterDriverPrivate *priv;
	struct _filter_rule *node;

	priv = CAMEL_FILTER_DRIVER_GET_PRIVATE (object);

	/* close all folders that were opened for appending */
	close_folders (CAMEL_FILTER_DRIVER (object));
	g_hash_table_destroy (priv->folders);

	g_hash_table_foreach (priv->globals, free_hash_strings, object);
	g_hash_table_destroy (priv->globals);

	g_hash_table_foreach (priv->only_once, free_hash_strings, object);
	g_hash_table_destroy (priv->only_once);

	e_sexp_unref (priv->eval);

	while ((node = (struct _filter_rule *) camel_dlist_remhead (&priv->rules))) {
		g_free (node->match);
		g_free (node->action);
		g_free (node->name);
		g_free (node);
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_filter_driver_parent_class)->finalize (object);
}

static void
camel_filter_driver_class_init (CamelFilterDriverClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelFilterDriverPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = filter_driver_dispose;
	object_class->finalize = filter_driver_finalize;
}

static void
camel_filter_driver_init (CamelFilterDriver *filter_driver)
{
	gint ii;

	filter_driver->priv = CAMEL_FILTER_DRIVER_GET_PRIVATE (filter_driver);

	camel_dlist_init (&filter_driver->priv->rules);

	filter_driver->priv->eval = e_sexp_new ();

	/* Load in builtin symbols */
	for (ii = 0; ii < G_N_ELEMENTS (symbols); ii++) {
		if (symbols[ii].type == 1) {
			e_sexp_add_ifunction (
				filter_driver->priv->eval, 0,
				symbols[ii].name, (ESExpIFunc *)
				symbols[ii].func, filter_driver);
		} else {
			e_sexp_add_function (
				filter_driver->priv->eval, 0,
				symbols[ii].name, symbols[ii].func,
				filter_driver);
		}
	}

	filter_driver->priv->globals =
		g_hash_table_new (g_str_hash, g_str_equal);

	filter_driver->priv->folders =
		g_hash_table_new (g_str_hash, g_str_equal);

	filter_driver->priv->only_once =
		g_hash_table_new (g_str_hash, g_str_equal);
}

/**
 * camel_filter_driver_new:
 *
 * Returns: A new CamelFilterDriver object
 **/
CamelFilterDriver *
camel_filter_driver_new (CamelSession *session)
{
	CamelFilterDriver *d;

	d = g_object_new (CAMEL_TYPE_FILTER_DRIVER, NULL);
	d->priv->session = g_object_ref (session);

	return d;
}

void
camel_filter_driver_set_folder_func (CamelFilterDriver *d, CamelFilterGetFolderFunc get_folder, gpointer data)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (d);

	p->get_folder = get_folder;
	p->data = data;
}

void
camel_filter_driver_set_logfile (CamelFilterDriver *d, FILE *logfile)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (d);

	p->logfile = logfile;
}

void
camel_filter_driver_set_status_func (CamelFilterDriver *d, CamelFilterStatusFunc *func, gpointer data)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (d);

	p->statusfunc = func;
	p->statusdata = data;
}

void
camel_filter_driver_set_shell_func (CamelFilterDriver *d, CamelFilterShellFunc *func, gpointer data)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (d);

	p->shellfunc = func;
	p->shelldata = data;
}

void
camel_filter_driver_set_play_sound_func (CamelFilterDriver *d, CamelFilterPlaySoundFunc *func, gpointer data)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (d);

	p->playfunc = func;
	p->playdata = data;
}

void
camel_filter_driver_set_system_beep_func (CamelFilterDriver *d, CamelFilterSystemBeepFunc *func, gpointer data)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (d);

	p->beep = func;
	p->beepdata = data;
}

void
camel_filter_driver_set_default_folder (CamelFilterDriver *d, CamelFolder *def)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (d);

	if (p->defaultfolder) {
		camel_folder_thaw (p->defaultfolder);
		g_object_unref (p->defaultfolder);
	}

	p->defaultfolder = def;

	if (p->defaultfolder) {
		camel_folder_freeze (p->defaultfolder);
		g_object_ref (p->defaultfolder);
	}
}

void
camel_filter_driver_add_rule(CamelFilterDriver *d, const gchar *name, const gchar *match, const gchar *action)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (d);
	struct _filter_rule *node;

	node = g_malloc(sizeof(*node));
	node->match = g_strdup(match);
	node->action = g_strdup(action);
	node->name = g_strdup(name);
	camel_dlist_addtail(&p->rules, (CamelDListNode *)node);
}

gint
camel_filter_driver_remove_rule_by_name (CamelFilterDriver *d, const gchar *name)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (d);
	struct _filter_rule *node;

	node = (struct _filter_rule *) p->rules.head;
	while (node->next) {
		if (!strcmp (node->name, name)) {
			camel_dlist_remove ((CamelDListNode *) node);
			g_free (node->match);
			g_free (node->action);
			g_free (node->name);
			g_free (node);

			return 0;
		}

		node = node->next;
	}

	return -1;
}

static void
report_status (CamelFilterDriver *driver, enum camel_filter_status_t status, gint pc, const gchar *desc, ...)
{
	/* call user-defined status report function */
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	va_list ap;
	gchar *str;

	if (p->statusfunc) {
		va_start (ap, desc);
		str = g_strdup_vprintf (desc, ap);
		p->statusfunc (driver, status, pc, str, p->statusdata);
		g_free (str);
	}
}

#if 0
void
camel_filter_driver_set_global (CamelFilterDriver *d, const gchar *name, const gchar *value)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (d);
	gchar *oldkey, *oldvalue;

	if (g_hash_table_lookup_extended (p->globals, name, (gpointer)&oldkey, (gpointer)&oldvalue)) {
		g_free (oldvalue);
		g_hash_table_insert (p->globals, oldkey, g_strdup (value));
	} else {
		g_hash_table_insert (p->globals, g_strdup (name), g_strdup (value));
	}
}
#endif

static ESExpResult *
do_delete (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	d(fprintf (stderr, "doing delete\n"));
	p->deleted = TRUE;
	camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Delete");

	return NULL;
}

static ESExpResult *
do_forward_to (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	d(fprintf (stderr, "marking message for forwarding\n"));

	/* requires one parameter, string with a destination address */
	if (argc < 1 || argv[0]->type != ESEXP_RES_STRING)
		return NULL;

	/* make sure we have the message... */
	if (p->message == NULL) {
		if (!(p->message = camel_folder_get_message (p->source, p->uid, &p->error)))
			return NULL;
	}

	camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Forward message to '%s'", argv[0]->value.string);
	camel_session_forward_to (p->session, p->source, p->message, argv[0]->value.string, &p->error);

	return NULL;
}

static ESExpResult *
do_copy (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	gint i;

	d(fprintf (stderr, "copying message...\n"));

	for (i = 0; i < argc; i++) {
		if (argv[i]->type == ESEXP_RES_STRING) {
			/* open folders we intent to copy to */
			gchar *folder = argv[i]->value.string;
			CamelFolder *outbox;

			outbox = open_folder (driver, folder);
			if (!outbox)
				break;

			if (outbox == p->source)
				break;

			if (!p->modified && p->uid && p->source && camel_folder_has_summary_capability (p->source)) {
				GPtrArray *uids;

				uids = g_ptr_array_new ();
				g_ptr_array_add (uids, (gchar *) p->uid);
				camel_folder_transfer_messages_to (p->source, uids, outbox, NULL, FALSE, &p->error);
				g_ptr_array_free (uids, TRUE);
			} else {
				if (p->message == NULL)
					p->message = camel_folder_get_message (p->source, p->uid, &p->error);

				if (!p->message)
					continue;

				camel_folder_append_message (outbox, p->message, p->info, NULL, &p->error);
			}

			if (p->error == NULL)
				p->copied = TRUE;

			camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Copy to folder %s",
						 folder);
		}
	}

	return NULL;
}

static ESExpResult *
do_move (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	gint i;

	d(fprintf (stderr, "moving message...\n"));

	for (i = 0; i < argc; i++) {
		if (argv[i]->type == ESEXP_RES_STRING) {
			/* open folders we intent to move to */
			gchar *folder = argv[i]->value.string;
			CamelFolder *outbox;
			gint last;

			outbox = open_folder (driver, folder);
			if (!outbox)
				break;

			if (outbox == p->source)
				break;

			/* only delete on last folder (only 1 can ever be supplied by ui currently) */
			last = (i == argc-1);

			if (!p->modified && p->uid && p->source && camel_folder_has_summary_capability (p->source)) {
				GPtrArray *uids;

				uids = g_ptr_array_new ();
				g_ptr_array_add (uids, (gchar *) p->uid);
				camel_folder_transfer_messages_to (
					p->source, uids, outbox, NULL,
					last, &p->error);
				g_ptr_array_free (uids, TRUE);
			} else {
				if (p->message == NULL)
					p->message = camel_folder_get_message (
						p->source, p->uid, &p->error);

				if (!p->message)
					continue;

				camel_folder_append_message (
					outbox, p->message, p->info,
					NULL, &p->error);

				if (p->error == NULL && last) {
					if (p->source && p->uid && camel_folder_has_summary_capability (p->source))
						camel_folder_set_message_flags(p->source, p->uid, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_SEEN, ~0);
					else
						camel_message_info_set_flags(p->info, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_FOLDER_FLAGGED, ~0);
				}
			}

			if (p->error == NULL) {
				p->moved = TRUE;
				camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Move to folder %s", folder);
			}
		}
	}

	/* implicit 'stop' with 'move' */
	camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Stopped processing");
	p->terminated = TRUE;

	return NULL;
}

static ESExpResult *
do_stop (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Stopped processing");
	d(fprintf (stderr, "terminating message processing\n"));
	p->terminated = TRUE;

	return NULL;
}

static ESExpResult *
do_label (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	d(fprintf (stderr, "setting label tag\n"));
	if (argc > 0 && argv[0]->type == ESEXP_RES_STRING) {
		/* This is a list of new labels, we should used these in case of passing in old names.
		   This all is required only because backward compatibility. */
		const gchar *new_labels[] = { "$Labelimportant", "$Labelwork", "$Labelpersonal", "$Labeltodo", "$Labellater", NULL};
		const gchar *label;
		gint i;

		label = argv[0]->value.string;

		for (i = 0; new_labels[i]; i++) {
			if (label && strcmp (new_labels[i] + 6, label) == 0) {
				label = new_labels[i];
				break;
			}
		}

		if (p->source && p->uid && camel_folder_has_summary_capability (p->source))
			camel_folder_set_message_user_flag (p->source, p->uid, label, TRUE);
		else
			camel_message_info_set_user_flag (p->info, label, TRUE);
		camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Set label to %s", label);
	}

	return NULL;
}

static ESExpResult *
do_color (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	d(fprintf (stderr, "setting color tag\n"));
	if (argc > 0 && argv[0]->type == ESEXP_RES_STRING) {
		if (p->source && p->uid && camel_folder_has_summary_capability (p->source))
			camel_folder_set_message_user_tag (p->source, p->uid, "color", argv[0]->value.string);
		else
			camel_message_info_set_user_tag(p->info, "color", argv[0]->value.string);
		camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Set color to %s", argv[0]->value.string);
	}

	return NULL;
}

static ESExpResult *
do_score (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	d(fprintf (stderr, "setting score tag\n"));
	if (argc > 0 && argv[0]->type == ESEXP_RES_INT) {
		gchar *value;

		value = g_strdup_printf ("%d", argv[0]->value.number);
		camel_message_info_set_user_tag(p->info, "score", value);
		camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Set score to %d", argv[0]->value.number);
		g_free (value);
	}

	return NULL;
}

static ESExpResult *
do_adjust_score(struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE(driver);

	d(fprintf (stderr, "adjusting score tag\n"));
	if (argc > 0 && argv[0]->type == ESEXP_RES_INT) {
		gchar *value;
		gint old;

		value = (gchar *)camel_message_info_user_tag(p->info, "score");
		old = value?atoi(value):0;
		value = g_strdup_printf ("%d", old+argv[0]->value.number);
		camel_message_info_set_user_tag(p->info, "score", value);
		camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Adjust score (%d) to %s", argv[0]->value.number, value);
		g_free (value);
	}

	return NULL;
}

static ESExpResult *
set_flag (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	guint32 flags;

	d(fprintf (stderr, "setting flag\n"));
	if (argc == 1 && argv[0]->type == ESEXP_RES_STRING) {
		flags = camel_system_flag (argv[0]->value.string);
		if (p->source && p->uid && camel_folder_has_summary_capability (p->source))
			camel_folder_set_message_flags (p->source, p->uid, flags, ~0);
		else
			camel_message_info_set_flags(p->info, flags | CAMEL_MESSAGE_FOLDER_FLAGGED, ~0);
		camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Set %s flag", argv[0]->value.string);
	}

	return NULL;
}

static ESExpResult *
unset_flag (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	guint32 flags;

	d(fprintf (stderr, "unsetting flag\n"));
	if (argc == 1 && argv[0]->type == ESEXP_RES_STRING) {
		flags = camel_system_flag (argv[0]->value.string);
		if (p->source && p->uid && camel_folder_has_summary_capability (p->source))
			camel_folder_set_message_flags (p->source, p->uid, flags, 0);
		else
			camel_message_info_set_flags(p->info, flags | CAMEL_MESSAGE_FOLDER_FLAGGED, 0);
		camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Unset %s flag", argv[0]->value.string);
	}

	return NULL;
}

#ifndef G_OS_WIN32
static void
child_setup_func (gpointer user_data)
{
	setsid ();
}
#else
#define child_setup_func NULL
#endif

typedef struct {
	gint child_status;
	GMainLoop *loop;
} child_watch_data_t;

static void
child_watch (GPid     pid,
	     gint     status,
	     gpointer data)
{
	child_watch_data_t *child_watch_data = data;

	g_spawn_close_pid (pid);

	child_watch_data->child_status = status;

	g_main_loop_quit (child_watch_data->loop);
}

static gint
pipe_to_system (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	gint i, pipe_to_child, pipe_from_child;
	CamelMimeMessage *message = NULL;
	CamelMimeParser *parser;
	CamelStream *stream, *mem;
	GPid child_pid;
	GError *error = NULL;
	GPtrArray *args;
	child_watch_data_t child_watch_data;
	GSource *source;
	GMainContext *context;

	if (argc < 1 || argv[0]->value.string[0] == '\0')
		return 0;

	/* make sure we have the message... */
	if (p->message == NULL) {
		if (!(p->message = camel_folder_get_message (p->source, p->uid, &p->error)))
			return -1;
	}

	args = g_ptr_array_new ();
	for (i = 0; i < argc; i++)
		g_ptr_array_add (args, argv[i]->value.string);
	g_ptr_array_add (args, NULL);

	if (!g_spawn_async_with_pipes (NULL,
				       (gchar **) args->pdata,
				       NULL,
				       G_SPAWN_DO_NOT_REAP_CHILD |
				       G_SPAWN_SEARCH_PATH |
				       G_SPAWN_STDERR_TO_DEV_NULL,
				       child_setup_func,
				       NULL,
				       &child_pid,
				       &pipe_to_child,
				       &pipe_from_child,
				       NULL,
				       &error)) {
		g_ptr_array_free (args, TRUE);

		g_set_error (
			&p->error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Failed to create child process '%s': %s"),
			argv[0]->value.string, error->message);
		g_error_free (error);
		return -1;
	}

	g_ptr_array_free (args, TRUE);

	stream = camel_stream_fs_new_with_fd (pipe_to_child);
	if (camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (p->message), stream, NULL) == -1) {
		g_object_unref (stream);
		close (pipe_from_child);
		goto wait;
	}

	if (camel_stream_flush (stream, NULL) == -1) {
		g_object_unref (stream);
		close (pipe_from_child);
		goto wait;
	}

	g_object_unref (stream);

	stream = camel_stream_fs_new_with_fd (pipe_from_child);
	mem = camel_stream_mem_new ();
	if (camel_stream_write_to_stream (stream, mem, NULL) == -1) {
		g_object_unref (stream);
		g_object_unref (mem);
		goto wait;
	}

	g_object_unref (stream);
	camel_stream_reset (mem, NULL);

	parser = camel_mime_parser_new ();
	camel_mime_parser_init_with_stream (parser, mem, NULL);
	camel_mime_parser_scan_from (parser, FALSE);
	g_object_unref (mem);

	message = camel_mime_message_new ();
	if (camel_mime_part_construct_from_parser ((CamelMimePart *) message, parser, NULL) == -1) {
		gint err = camel_mime_parser_errno (parser);
		g_set_error (
			&p->error, G_IO_ERROR,
			g_io_error_from_errno (err),
			_("Invalid message stream received from %s: %s"),
			argv[0]->value.string, g_strerror (err));
		g_object_unref (message);
		message = NULL;
	} else {
		g_object_unref (p->message);
		p->message = message;
		p->modified = TRUE;
	}

	g_object_unref (parser);

 wait:
	context = g_main_context_new ();
	child_watch_data.loop = g_main_loop_new (context, FALSE);
	g_main_context_unref (context);

	source = g_child_watch_source_new (child_pid);
	g_source_set_callback (source, (GSourceFunc) child_watch, &child_watch_data, NULL);
	g_source_attach (source, g_main_loop_get_context (child_watch_data.loop));
	g_source_unref (source);

	g_main_loop_run (child_watch_data.loop);
	g_main_loop_unref (child_watch_data.loop);

#ifndef G_OS_WIN32
	if (message && WIFEXITED (child_watch_data.child_status))
		return WEXITSTATUS (child_watch_data.child_status);
	else
		return -1;
#else
	return child_watch_data.child_status;
#endif
}

static ESExpResult *
pipe_message (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	gint i;

	/* make sure all args are strings */
	for (i = 0; i < argc; i++) {
		if (argv[i]->type != ESEXP_RES_STRING)
			return NULL;
	}

	camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Piping message to %s", argv[0]->value.string);
	pipe_to_system (f, argc, argv, driver);

	return NULL;
}

static ESExpResult *
do_shell (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	GString *command;
	GPtrArray *args;
	gint i;

	d(fprintf (stderr, "executing shell command\n"));

	command = g_string_new ("");

	args = g_ptr_array_new ();

	/* make sure all args are strings */
	for (i = 0; i < argc; i++) {
		if (argv[i]->type != ESEXP_RES_STRING)
			goto done;

		g_ptr_array_add (args, argv[i]->value.string);

		g_string_append (command, argv[i]->value.string);
		g_string_append_c (command, ' ');
	}

	g_string_truncate (command, command->len - 1);

	if (p->shellfunc && argc >= 1) {
		p->shellfunc (driver, argc, (gchar **) args->pdata, p->shelldata);
		camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Executing shell command: [%s]",
					 command->str);
	}

 done:

	g_ptr_array_free (args, TRUE);
	g_string_free (command, TRUE);

	return NULL;
}

static ESExpResult *
do_beep (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	d(fprintf (stderr, "beep\n"));

	if (p->beep) {
		p->beep (driver, p->beepdata);
		camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Beep");
	}

	return NULL;
}

static ESExpResult *
play_sound (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	d(fprintf (stderr, "play sound\n"));

	if (p->playfunc && argc == 1 && argv[0]->type == ESEXP_RES_STRING) {
		p->playfunc (driver, argv[0]->value.string, p->playdata);
		camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Play sound");
	}

	return NULL;
}

static ESExpResult *
do_only_once (struct _ESExp *f, gint argc, struct _ESExpResult **argv, CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	d(fprintf (stderr, "only once\n"));

	if (argc == 2 && !g_hash_table_lookup (p->only_once, argv[0]->value.string))
		g_hash_table_insert (p->only_once, g_strdup (argv[0]->value.string),
				     g_strdup (argv[1]->value.string));

	return NULL;
}

static CamelFolder *
open_folder (CamelFilterDriver *driver, const gchar *folder_url)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	CamelFolder *camelfolder;

	/* we have a lookup table of currently open folders */
	camelfolder = g_hash_table_lookup (p->folders, folder_url);
	if (camelfolder)
		return camelfolder == FOLDER_INVALID?NULL:camelfolder;

	/* if we have a default folder, ignore exceptions.  This is so
	   a bad filter rule on pop or local delivery doesn't result
	   in duplicate mails, just mail going to inbox.  Otherwise,
	   we want to know about exceptions and abort processing */
	if (p->defaultfolder) {
		camelfolder = p->get_folder (driver, folder_url, p->data, NULL);
	} else {
		camelfolder = p->get_folder (driver, folder_url, p->data, &p->error);
	}

	if (camelfolder) {
		g_hash_table_insert (p->folders, g_strdup (folder_url), camelfolder);
		camel_folder_freeze (camelfolder);
	} else {
		g_hash_table_insert (p->folders, g_strdup (folder_url), FOLDER_INVALID);
	}

	return camelfolder;
}

static void
close_folder (gpointer key, gpointer value, gpointer data)
{
	CamelFolder *folder = value;
	CamelFilterDriver *driver = data;
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	p->closed++;
	g_free (key);

	if (folder != FOLDER_INVALID) {
		camel_folder_sync (folder, FALSE, (p->error != NULL) ? NULL : &p->error);
		camel_folder_thaw (folder);
		g_object_unref (folder);
	}

	report_status(driver, CAMEL_FILTER_STATUS_PROGRESS, g_hash_table_size(p->folders)* 100 / p->closed, _("Syncing folders"));
}

/* flush/close all folders */
static gint
close_folders (CamelFilterDriver *driver)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	report_status(driver, CAMEL_FILTER_STATUS_PROGRESS, 0, _("Syncing folders"));

	p->closed = 0;
	g_hash_table_foreach (p->folders, close_folder, driver);
	g_hash_table_destroy (p->folders);
	p->folders = g_hash_table_new (g_str_hash, g_str_equal);

	/* FIXME: status from driver */
	return 0;
}

#if 0
static void
free_key (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
}
#endif

static void
camel_filter_driver_log (CamelFilterDriver *driver, enum filter_log_t status, const gchar *desc, ...)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);

	if (p->logfile) {
		gchar *str = NULL;

		if (desc) {
			va_list ap;

			va_start (ap, desc);
			str = g_strdup_vprintf (desc, ap);
		}

		switch (status) {
		case FILTER_LOG_START: {
			/* write log header */
			const gchar *subject = NULL;
			const gchar *from = NULL;
			gchar date[50];
			time_t t;

			/* FIXME: does this need locking?  Probably */

			from = camel_message_info_from (p->info);
			subject = camel_message_info_subject (p->info);

			time (&t);
			strftime (date, 49, "%a, %d %b %Y %H:%M:%S", localtime (&t));
			fprintf (p->logfile, "Applied filter \"%s\" to message from %s - \"%s\" at %s\n",
				 str, from ? from : "unknown", subject ? subject : "", date);

			break;
		}
		case FILTER_LOG_ACTION:
			fprintf (p->logfile, "Action: %s\n", str);
			break;
		case FILTER_LOG_END:
			fprintf (p->logfile, "\n");
			break;
		default:
			/* nothing else is loggable */
			break;
		}

		g_free (str);
	}
}

struct _run_only_once {
	CamelFilterDriver *driver;
	GError *error;
};

static gboolean
run_only_once (gpointer key, gchar *action, struct _run_only_once *data)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (data->driver);
	ESExpResult *r;

	d(printf ("evaluating: %s\n\n", action));

	e_sexp_input_text (p->eval, action, strlen (action));
	if (e_sexp_parse (p->eval) == -1) {
		if (data->error == NULL)
			g_set_error (
				&data->error,
				CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Error parsing filter: %s: %s"),
				e_sexp_error (p->eval), action);
		goto done;
	}

	r = e_sexp_eval (p->eval);
	if (r == NULL) {
		if (data->error == NULL)
			g_set_error (
				&data->error,
				CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Error executing filter: %s: %s"),
				e_sexp_error (p->eval), action);
		goto done;
	}

	e_sexp_result_free (p->eval, r);

 done:

	g_free (key);
	g_free (action);

	return TRUE;
}

/**
 * camel_filter_driver_flush:
 * @driver:
 * @error: return location for a #GError, or %NULL
 *
 * Flush all of the only-once filter actions.
 **/
void
camel_filter_driver_flush (CamelFilterDriver *driver,
                           GError **error)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	struct _run_only_once data;

	if (!p->only_once)
		return;

	data.driver = driver;
	data.error = NULL;

	g_hash_table_foreach_remove (p->only_once, (GHRFunc) run_only_once, &data);

	if (data.error != NULL)
		g_propagate_error (error, data.error);
}

static gint
decode_flags_from_xev(const gchar *xev, CamelMessageInfoBase *mi)
{
	guint32 uid, flags = 0;
	gchar *header;

	/* check for uid/flags */
	header = camel_header_token_decode(xev);
	if (!(header && strlen(header) == strlen("00000000-0000")
	    && sscanf(header, "%08x-%04x", &uid, &flags) == 2)) {
		g_free(header);
		return 0;
	}
	g_free(header);

	mi->flags = flags;
	return 0;
}

/**
 * camel_filter_driver_filter_mbox:
 * @driver: CamelFilterDriver
 * @mbox: mbox filename to be filtered
 * @original_source_url:
 * @error: return location for a #GError, or %NULL
 *
 * Filters an mbox file based on rules defined in the FilterDriver
 * object. Is more efficient as it doesn't need to open the folder
 * through Camel directly.
 *
 * Returns: -1 if errors were encountered during filtering,
 * otherwise returns 0.
 *
 **/
gint
camel_filter_driver_filter_mbox (CamelFilterDriver *driver,
                                 const gchar *mbox,
                                 const gchar *original_source_url,
                                 GError **error)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	CamelMimeParser *mp = NULL;
	gchar *source_url = NULL;
	gint fd = -1;
	gint i = 0;
	struct stat st;
	gint status;
	goffset last = 0;
	gint ret = -1;

	fd = g_open (mbox, O_RDONLY|O_BINARY, 0);
	if (fd == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Unable to open spool folder"));
		goto fail;
	}
	/* to get the filesize */
	fstat (fd, &st);

	mp = camel_mime_parser_new ();
	camel_mime_parser_scan_from (mp, TRUE);
	if (camel_mime_parser_init_with_fd (mp, fd) == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Unable to process spool folder"));
		goto fail;
	}
	fd = -1;

	source_url = g_filename_to_uri (mbox, NULL, NULL);

	while (camel_mime_parser_step (mp, 0, 0) == CAMEL_MIME_PARSER_STATE_FROM) {
		CamelMessageInfo *info;
		CamelMimeMessage *message;
		CamelMimePart *mime_part;
		gint pc = 0;
		const gchar *xev;
		GError *local_error = NULL;

		if (st.st_size > 0)
			pc = (gint)(100.0 * ((double)camel_mime_parser_tell (mp) / (double)st.st_size));

		report_status (driver, CAMEL_FILTER_STATUS_START, pc, _("Getting message %d (%d%%)"), i, pc);

		message = camel_mime_message_new ();
		mime_part = CAMEL_MIME_PART (message);

		if (camel_mime_part_construct_from_parser (mime_part, mp, error) == -1) {
			report_status (driver, CAMEL_FILTER_STATUS_END, 100, _("Failed on message %d"), i);
			g_object_unref (message);
			goto fail;
		}

		info = camel_message_info_new_from_header(NULL, mime_part->headers);
		/* Try and see if it has X-Evolution headers */
		xev = camel_header_raw_find(&mime_part->headers, "X-Evolution", NULL);
		if (xev)
			decode_flags_from_xev (xev, (CamelMessageInfoBase *)info);

		((CamelMessageInfoBase *)info)->size = camel_mime_parser_tell(mp) - last;

		last = camel_mime_parser_tell(mp);
		status = camel_filter_driver_filter_message (
			driver, message, info, NULL, NULL, source_url,
			original_source_url ? original_source_url : source_url,
			&local_error);
		g_object_unref (message);
		if (local_error != NULL || status == -1) {
			report_status (
				driver, CAMEL_FILTER_STATUS_END, 100,
				_("Failed on message %d"), i);
			camel_message_info_free (info);
			g_propagate_error (error, local_error);
			goto fail;
		}

		i++;

		/* skip over the FROM_END state */
		camel_mime_parser_step (mp, 0, 0);

		camel_message_info_free (info);
	}

	if (p->defaultfolder) {
		report_status(driver, CAMEL_FILTER_STATUS_PROGRESS, 100, _("Syncing folder"));
		camel_folder_sync(p->defaultfolder, FALSE, NULL);
	}

	report_status (driver, CAMEL_FILTER_STATUS_END, 100, _("Complete"));

	ret = 0;
fail:
	g_free (source_url);
	if (fd != -1)
		close (fd);
	if (mp)
		g_object_unref (mp);

	return ret;
}

/**
 * camel_filter_driver_filter_folder:
 * @driver: CamelFilterDriver
 * @folder: CamelFolder to be filtered
 * @cache: UID cache (needed for POP folders)
 * @uids: message uids to be filtered or NULL (as a shortcut to filter all messages)
 * @remove: TRUE to mark filtered messages as deleted
 * @error: return location for a #GError, or %NULL
 *
 * Filters a folder based on rules defined in the FilterDriver
 * object.
 *
 * Returns: -1 if errors were encountered during filtering,
 * otherwise returns 0.
 *
 **/
gint
camel_filter_driver_filter_folder (CamelFilterDriver *driver,
                                   CamelFolder *folder,
                                   CamelUIDCache *cache,
                                   GPtrArray *uids,
                                   gboolean remove,
                                   GError **error)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	gboolean freeuids = FALSE;
	CamelMessageInfo *info;
	gchar *source_url, *service_url;
	gint status = 0;
	CamelURL *url;
	gint i;

	service_url = camel_service_get_url (CAMEL_SERVICE (camel_folder_get_parent_store (folder)));
	url = camel_url_new (service_url, NULL);
	g_free (service_url);

	source_url = camel_url_to_string (url, CAMEL_URL_HIDE_ALL);
	camel_url_free (url);

	if (uids == NULL) {
		uids = camel_folder_get_uids (folder);
		freeuids = TRUE;
	}

	for (i = 0; i < uids->len; i++) {
		gint pc = (100 * i)/uids->len;
		GError *local_error = NULL;

		report_status (driver, CAMEL_FILTER_STATUS_START, pc, _("Getting message %d of %d"), i+1,
			       uids->len);

		if (camel_folder_has_summary_capability (folder))
			info = camel_folder_get_message_info (folder, uids->pdata[i]);
		else
			info = NULL;

		status = camel_filter_driver_filter_message (
			driver, NULL, info, uids->pdata[i],
			folder, source_url, source_url, &local_error);

		if (camel_folder_has_summary_capability (folder))
			camel_folder_free_message_info (folder, info);

		if (local_error != NULL || status == -1) {
			report_status (
				driver, CAMEL_FILTER_STATUS_END, 100,
				_("Failed at message %d of %d"),
				i+1, uids->len);
			g_propagate_error (error, local_error);
			status = -1;
			break;
		}

		if (remove)
			camel_folder_set_message_flags (folder, uids->pdata[i],
							CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN, ~0);

		if (cache) {
			camel_uid_cache_save_uid (cache, uids->pdata[i]);
			if (i%10 == 0)
				camel_uid_cache_save (cache); /* Save list every 10 mails */
		}
	}

	if (p->defaultfolder) {
		report_status (driver, CAMEL_FILTER_STATUS_PROGRESS, 100, _("Syncing folder"));
		camel_folder_sync (p->defaultfolder, FALSE, NULL);
	}

	if (i == uids->len)
		report_status (driver, CAMEL_FILTER_STATUS_END, 100, _("Complete"));

	if (freeuids)
		camel_folder_free_uids (folder, uids);

	g_free (source_url);

	return status;
}

struct _get_message {
	struct _CamelFilterDriverPrivate *p;
	const gchar *source_url;
};

static CamelMimeMessage *
get_message_cb (gpointer data, GError **error)
{
	struct _get_message *msgdata = data;
	struct _CamelFilterDriverPrivate *p = msgdata->p;
	const gchar *source_url = msgdata->source_url;
	CamelMimeMessage *message;

	if (p->message) {
		message = g_object_ref (p->message);
	} else {
		const gchar *uid;

		if (p->uid)
			uid = p->uid;
		else
			uid = camel_message_info_uid (p->info);

		message = camel_folder_get_message (p->source, uid, error);
	}

	if (source_url && message && camel_mime_message_get_source (message) == NULL)
		camel_mime_message_set_source (message, source_url);

	return message;
}

/**
 * camel_filter_driver_filter_message:
 * @driver: CamelFilterDriver
 * @message: message to filter or NULL
 * @info: message info or NULL
 * @uid: message uid or NULL
 * @source: source folder or NULL
 * @source_url: url of source folder or NULL
 * @original_source_url: url of original source folder (pre-movemail) or NULL
 * @error: return location for a #GError, or %NULL
 *
 * Filters a message based on rules defined in the FilterDriver
 * object. If the source folder (@source) and the uid (@uid) are
 * provided, the filter will operate on the CamelFolder (which in
 * certain cases is more efficient than using the default
 * camel_folder_append_message() function).
 *
 * Returns: -1 if errors were encountered during filtering,
 * otherwise returns 0.
 *
 **/
gint
camel_filter_driver_filter_message (CamelFilterDriver *driver,
                                    CamelMimeMessage *message,
                                    CamelMessageInfo *info,
                                    const gchar *uid,
                                    CamelFolder *source,
                                    const gchar *source_url,
                                    const gchar *original_source_url,
                                    GError **error)
{
	struct _CamelFilterDriverPrivate *p = CAMEL_FILTER_DRIVER_GET_PRIVATE (driver);
	struct _filter_rule *node;
	gboolean freeinfo = FALSE;
	gboolean filtered = FALSE;
	ESExpResult *r;
	gint result;

	/* FIXME: make me into a g_return_if_fail/g_assert or whatever... */
	if (message == NULL && (source == NULL || uid == NULL)) {
		g_warning ("there is no way to fetch the message using the information provided...");
		return -1;
	}

	if (info == NULL) {
		struct _camel_header_raw *h;

		if (message) {
			g_object_ref (message);
		} else {
			message = camel_folder_get_message (source, uid, error);
			if (!message)
				return -1;
		}

		h = CAMEL_MIME_PART (message)->headers;
		info = camel_message_info_new_from_header (NULL, h);
		freeinfo = TRUE;
	} else {
		if (camel_message_info_flags(info) & CAMEL_MESSAGE_DELETED)
			return 0;

		uid = camel_message_info_uid (info);

		if (message)
			g_object_ref (message);
	}

	p->terminated = FALSE;
	p->deleted = FALSE;
	p->copied = FALSE;
	p->moved = FALSE;
	p->message = message;
	p->info = info;
	p->uid = uid;
	p->source = source;

	if (message && original_source_url && camel_mime_message_get_source (message) == NULL)
		camel_mime_message_set_source (message, original_source_url);

	node = (struct _filter_rule *) p->rules.head;
	result = CAMEL_SEARCH_NOMATCH;
	while (node->next && !p->terminated) {
		struct _get_message data;

		d(printf("applying rule %s\naction %s\n", node->match, node->action));

		data.p = p;
		data.source_url = original_source_url;

		result = camel_filter_search_match (
			p->session, get_message_cb, &data, p->info,
			original_source_url ? original_source_url : source_url,
			node->match, &p->error);

		switch (result) {
		case CAMEL_SEARCH_ERROR:
			goto error;
		case CAMEL_SEARCH_MATCHED:
			filtered = TRUE;
			camel_filter_driver_log (driver, FILTER_LOG_START, "%s", node->name);

			if (camel_debug(":filter"))
				printf("filtering '%s' applying rule %s\n",
				       camel_message_info_subject(info)?camel_message_info_subject(info):"?no subject?", node->name);

			/* perform necessary filtering actions */
			e_sexp_input_text (p->eval, node->action, strlen (node->action));
			if (e_sexp_parse (p->eval) == -1) {
				g_set_error (
					error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
					_("Error parsing filter: %s: %s"),
					e_sexp_error (p->eval), node->action);
				goto error;
			}
			r = e_sexp_eval (p->eval);
			if (p->error != NULL)
				goto error;

			if (r == NULL) {
				g_set_error (
					error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
					_("Error executing filter: %s: %s"),
					e_sexp_error (p->eval), node->action);
				goto error;
			}
			e_sexp_result_free (p->eval, r);
		default:
			break;
		}

		node = node->next;
	}

	/* *Now* we can set the DELETED flag... */
	if (p->deleted) {
		if (p->source && p->uid && camel_folder_has_summary_capability (p->source))
			camel_folder_set_message_flags(p->source, p->uid, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_SEEN, ~0);
		else
			camel_message_info_set_flags(info, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_FOLDER_FLAGGED, ~0);
	}

	/* Logic: if !Moved and there exists a default folder... */
	if (!(p->copied && p->deleted) && !p->moved && p->defaultfolder) {
		/* copy it to the default inbox */
		filtered = TRUE;
		camel_filter_driver_log (driver, FILTER_LOG_ACTION, "Copy to default folder");

		if (camel_debug(":filter"))
			printf("filtering '%s' copy %s to default folder\n",
			       camel_message_info_subject(info)?camel_message_info_subject(info):"?no subject?",
			       p->modified?"modified message":"");

		if (!p->modified && p->uid && p->source && camel_folder_has_summary_capability (p->source)) {
			GPtrArray *uids;

			uids = g_ptr_array_new ();
			g_ptr_array_add (uids, (gchar *) p->uid);
			camel_folder_transfer_messages_to (
				p->source, uids, p->defaultfolder,
				NULL, FALSE, &p->error);
			g_ptr_array_free (uids, TRUE);
		} else {
			if (p->message == NULL) {
				p->message = camel_folder_get_message (source, uid, error);
				if (!p->message)
					goto error;
			}

			camel_folder_append_message (
				p->defaultfolder, p->message,
				p->info, NULL, &p->error);
		}
	}

	if (p->message)
		g_object_unref (p->message);

	if (freeinfo)
		camel_message_info_free (info);

	return 0;

 error:
	if (filtered)
		camel_filter_driver_log (driver, FILTER_LOG_END, NULL);

	if (p->message)
		g_object_unref (p->message);

	if (freeinfo)
		camel_message_info_free (info);

	g_propagate_error (error, p->error);
	p->error = NULL;

	return -1;
}

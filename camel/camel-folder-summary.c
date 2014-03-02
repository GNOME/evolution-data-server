/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-file-utils.h"
#include "camel-folder-summary.h"
#include "camel-folder.h"
#include "camel-iconv.h"
#include "camel-mime-filter-basic.h"
#include "camel-mime-filter-charset.h"
#include "camel-mime-filter-html.h"
#include "camel-mime-filter-index.h"
#include "camel-mime-filter.h"
#include "camel-mime-message.h"
#include "camel-multipart.h"
#include "camel-session.h"
#include "camel-stream-filter.h"
#include "camel-stream-mem.h"
#include "camel-stream-null.h"
#include "camel-string-utils.h"
#include "camel-store.h"
#include "camel-vee-folder.h"
#include "camel-vtrash-folder.h"
#include "camel-mime-part-utils.h"

#define CAMEL_FOLDER_SUMMARY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_FOLDER_SUMMARY, CamelFolderSummaryPrivate))

/* Make 5 minutes as default cache drop */
#define SUMMARY_CACHE_DROP 300
#define dd(x) if (camel_debug("sync")) x

struct _CamelFolderSummaryPrivate {
	GHashTable *filter_charset;	/* CamelMimeFilterCharset's indexed by source charset */

	struct _CamelMimeFilter *filter_index;
	struct _CamelMimeFilter *filter_64;
	struct _CamelMimeFilter *filter_qp;
	struct _CamelMimeFilter *filter_uu;
	struct _CamelMimeFilter *filter_save;
	struct _CamelMimeFilter *filter_html;

	struct _CamelStream *filter_stream;

	struct _CamelIndex *index;

	GRecMutex summary_lock;	/* for the summary hashtable/array */
	GRecMutex filter_lock;	/* for accessing any of the filtering/indexing stuff, since we share them */

	gboolean need_preview;
	GHashTable *preview_updates;

	guint32 nextuid;	/* next uid? */
	guint32 saved_count;	/* how many were saved/loaded */
	guint32 unread_count;	/* handy totals */
	guint32 deleted_count;
	guint32 junk_count;
	guint32 junk_not_deleted_count;
	guint32 visible_count;

	gboolean build_content;	/* do we try and parse/index the content, or not? */

	GHashTable *uids; /* uids of all known message infos; the 'value' are used flags for the message info */
	GHashTable *loaded_infos; /* uid->CamelMessageInfo *, those currently in memory */

	struct _CamelFolder *folder; /* parent folder, for events */
	time_t cache_load_time;
	guint timeout_handle;
};

/* this should probably be conditional on it existing */
#define USE_BSEARCH

#define d(x)
#define io(x)			/* io debug */
#define w(x)

#define CAMEL_FOLDER_SUMMARY_VERSION (14)

/* trivial lists, just because ... */
struct _node {
	struct _node *next;
};

static void cfs_schedule_info_release_timer (CamelFolderSummary *summary);

static struct _node *my_list_append (struct _node **list, struct _node *n);
static gint my_list_size (struct _node **list);

static CamelMessageInfo * message_info_new_from_header (CamelFolderSummary *, struct _camel_header_raw *);
static CamelMessageInfo * message_info_new_from_parser (CamelFolderSummary *, CamelMimeParser *);
static CamelMessageInfo * message_info_new_from_message (CamelFolderSummary *summary, CamelMimeMessage *msg, const gchar *bodystructure);
static void		  message_info_free (CamelFolderSummary *, CamelMessageInfo *);

static CamelMessageContentInfo * content_info_new_from_header (CamelFolderSummary *, struct _camel_header_raw *);
static CamelMessageContentInfo * content_info_new_from_parser (CamelFolderSummary *, CamelMimeParser *);
static CamelMessageContentInfo * content_info_new_from_message (CamelFolderSummary *summary, CamelMimePart *mp);
static void			 content_info_free (CamelFolderSummary *, CamelMessageContentInfo *);

static gint save_message_infos_to_db (CamelFolderSummary *summary, GError **error);
static gint camel_read_mir_callback (gpointer  ref, gint ncol, gchar ** cols, gchar ** name);

static gchar *next_uid_string (CamelFolderSummary *summary);

static CamelMessageContentInfo * summary_build_content_info (CamelFolderSummary *summary, CamelMessageInfo *msginfo, CamelMimeParser *mp);
static CamelMessageContentInfo * summary_build_content_info_message (CamelFolderSummary *summary, CamelMessageInfo *msginfo, CamelMimePart *object);

static CamelMessageInfo * message_info_from_uid (CamelFolderSummary *summary, const gchar *uid);

enum {
	PROP_0,
	PROP_FOLDER,
	PROP_SAVED_COUNT,
	PROP_UNREAD_COUNT,
	PROP_DELETED_COUNT,
	PROP_JUNK_COUNT,
	PROP_JUNK_NOT_DELETED_COUNT,
	PROP_VISIBLE_COUNT,
	PROP_BUILD_CONTENT,
	PROP_NEED_PREVIEW
};

G_DEFINE_TYPE (CamelFolderSummary, camel_folder_summary, G_TYPE_OBJECT)

static gboolean
remove_each_item (gpointer uid,
                  gpointer mi,
                  gpointer user_data)
{
	GSList **to_remove_infos = user_data;

	*to_remove_infos = g_slist_prepend (*to_remove_infos, mi);

	return TRUE;
}

static void
remove_all_loaded (CamelFolderSummary *summary)
{
	GSList *to_remove_infos = NULL;

	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	camel_folder_summary_lock (summary);

	g_hash_table_foreach_remove (summary->priv->loaded_infos, remove_each_item, &to_remove_infos);

	g_slist_foreach (to_remove_infos, (GFunc) camel_message_info_unref, NULL);
	g_slist_free (to_remove_infos);

	camel_folder_summary_unlock (summary);
}

static void
free_o_name (gpointer key,
             gpointer value,
             gpointer data)
{
	g_object_unref (value);
	g_free (key);
}

static void
folder_summary_dispose (GObject *object)
{
	CamelFolderSummaryPrivate *priv;

	priv = CAMEL_FOLDER_SUMMARY_GET_PRIVATE (object);

	if (priv->timeout_handle) {
		/* this should not happen, because the release timer
		 * holds a reference on object */
		g_source_remove (priv->timeout_handle);
		priv->timeout_handle = 0;
	}

	g_clear_object (&priv->filter_index);
	g_clear_object (&priv->filter_64);
	g_clear_object (&priv->filter_qp);
	g_clear_object (&priv->filter_uu);
	g_clear_object (&priv->filter_save);
	g_clear_object (&priv->filter_html);
	g_clear_object (&priv->filter_stream);
	g_clear_object (&priv->filter_index);

	if (priv->folder) {
		g_object_weak_unref (G_OBJECT (priv->folder), (GWeakNotify) g_nullify_pointer, &priv->folder);
		priv->folder = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_folder_summary_parent_class)->dispose (object);
}

static void
folder_summary_finalize (GObject *object)
{
	CamelFolderSummary *summary = CAMEL_FOLDER_SUMMARY (object);
	CamelFolderSummaryPrivate *priv = summary->priv;

	g_hash_table_destroy (priv->uids);
	remove_all_loaded (summary);
	g_hash_table_destroy (priv->loaded_infos);

	g_hash_table_foreach (priv->filter_charset, free_o_name, NULL);
	g_hash_table_destroy (priv->filter_charset);

	g_hash_table_destroy (priv->preview_updates);

	g_rec_mutex_clear (&priv->summary_lock);
	g_rec_mutex_clear (&priv->filter_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_folder_summary_parent_class)->finalize (object);
}

static void
folder_summary_set_folder (CamelFolderSummary *summary,
                           CamelFolder *folder)
{
	g_return_if_fail (summary->priv->folder == NULL);
	/* folder can be NULL in certain cases, see maildir-store */

	summary->priv->folder = folder;
	if (folder)
		g_object_weak_ref (G_OBJECT (folder), (GWeakNotify) g_nullify_pointer, &summary->priv->folder);
}

static void
folder_summary_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FOLDER:
			folder_summary_set_folder (
				CAMEL_FOLDER_SUMMARY (object),
				CAMEL_FOLDER (g_value_get_object (value)));
			return;

		case PROP_BUILD_CONTENT:
			camel_folder_summary_set_build_content (
				CAMEL_FOLDER_SUMMARY (object),
				g_value_get_boolean (value));
			return;

		case PROP_NEED_PREVIEW:
			camel_folder_summary_set_need_preview (
				CAMEL_FOLDER_SUMMARY (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
folder_summary_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FOLDER:
			g_value_set_object (
				value,
				camel_folder_summary_get_folder (
				CAMEL_FOLDER_SUMMARY (object)));
			return;

		case PROP_SAVED_COUNT:
			g_value_set_uint (
				value,
				camel_folder_summary_get_saved_count (
				CAMEL_FOLDER_SUMMARY (object)));
			return;

		case PROP_UNREAD_COUNT:
			g_value_set_uint (
				value,
				camel_folder_summary_get_unread_count (
				CAMEL_FOLDER_SUMMARY (object)));
			return;

		case PROP_DELETED_COUNT:
			g_value_set_uint (
				value,
				camel_folder_summary_get_deleted_count (
				CAMEL_FOLDER_SUMMARY (object)));
			return;

		case PROP_JUNK_COUNT:
			g_value_set_uint (
				value,
				camel_folder_summary_get_junk_count (
				CAMEL_FOLDER_SUMMARY (object)));
			return;

		case PROP_JUNK_NOT_DELETED_COUNT:
			g_value_set_uint (
				value,
				camel_folder_summary_get_junk_not_deleted_count (
				CAMEL_FOLDER_SUMMARY (object)));
			return;

		case PROP_VISIBLE_COUNT:
			g_value_set_uint (
				value,
				camel_folder_summary_get_visible_count (
				CAMEL_FOLDER_SUMMARY (object)));
			return;

		case PROP_BUILD_CONTENT:
			g_value_set_boolean (
				value,
				camel_folder_summary_get_build_content (
				CAMEL_FOLDER_SUMMARY (object)));
			return;

		case PROP_NEED_PREVIEW:
			g_value_set_boolean (
				value,
				camel_folder_summary_get_need_preview (
				CAMEL_FOLDER_SUMMARY (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static gboolean
is_in_memory_summary (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);

	return (summary->flags & CAMEL_FOLDER_SUMMARY_IN_MEMORY_ONLY) != 0;
}

#define UPDATE_COUNTS_ADD		(1)
#define UPDATE_COUNTS_SUB		(2)
#define UPDATE_COUNTS_ADD_WITHOUT_TOTAL (3)
#define UPDATE_COUNTS_SUB_WITHOUT_TOTAL (4)

static gboolean
folder_summary_update_counts_by_flags (CamelFolderSummary *summary,
                                       guint32 flags,
                                       gint op_type)
{
	gint unread = 0, deleted = 0, junk = 0;
	gboolean is_junk_folder = FALSE, is_trash_folder = FALSE;
	gboolean subtract = op_type == UPDATE_COUNTS_SUB || op_type == UPDATE_COUNTS_SUB_WITHOUT_TOTAL;
	gboolean without_total = op_type == UPDATE_COUNTS_ADD_WITHOUT_TOTAL || op_type == UPDATE_COUNTS_SUB_WITHOUT_TOTAL;
	gboolean changed = FALSE;
	GObject *summary_object;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);

	summary_object = G_OBJECT (summary);

	if (summary->priv->folder && CAMEL_IS_VTRASH_FOLDER (summary->priv->folder)) {
		CamelVTrashFolder *vtrash = CAMEL_VTRASH_FOLDER (summary->priv->folder);

		is_junk_folder = vtrash && vtrash->type == CAMEL_VTRASH_FOLDER_JUNK;
		is_trash_folder = vtrash && vtrash->type == CAMEL_VTRASH_FOLDER_TRASH;
	}

	if (!(flags & CAMEL_MESSAGE_SEEN))
		unread = subtract ? -1 : 1;

	if (flags & CAMEL_MESSAGE_DELETED)
		deleted = subtract ? -1 : 1;

	if (flags & CAMEL_MESSAGE_JUNK)
		junk = subtract ? -1 : 1;

	dd (printf ("%p: %d %d %d | %d %d %d \n", (gpointer) summary, unread, deleted, junk, summary->priv->unread_count, summary->priv->visible_count, summary->priv->saved_count));

	g_object_freeze_notify (summary_object);

	if (deleted) {
		summary->priv->deleted_count += deleted;
		g_object_notify (summary_object, "deleted-count");
		changed = TRUE;
	}

	if (junk) {
		summary->priv->junk_count += junk;
		g_object_notify (summary_object, "junk-count");
		changed = TRUE;
	}

	if (junk && !deleted) {
		summary->priv->junk_not_deleted_count += junk;
		g_object_notify (summary_object, "junk-not-deleted-count");
		changed = TRUE;
	}

	if (!junk && !deleted) {
		summary->priv->visible_count += subtract ? -1 : 1;
		g_object_notify (summary_object, "visible-count");
		changed = TRUE;
	}

	if (junk && !is_junk_folder)
		unread = 0;
	if (deleted && !is_trash_folder)
		unread = 0;

	if (unread) {
		summary->priv->unread_count += unread;
		g_object_notify (summary_object, "unread-count");
		changed = TRUE;
	}

	if (!without_total) {
		summary->priv->saved_count += subtract ? -1 : 1;
		g_object_notify (summary_object, "saved-count");
		changed = TRUE;
	}

	if (changed)
		camel_folder_summary_touch (summary);

	g_object_thaw_notify (summary_object);

	dd (printf ("%p: %d %d %d | %d %d %d\n", (gpointer) summary, unread, deleted, junk, summary->priv->unread_count, summary->priv->visible_count, summary->priv->saved_count));

	return changed;
}

static gboolean
summary_header_from_db (CamelFolderSummary *summary,
                        CamelFIRecord *record)
{
	io (printf ("Loading header from db \n"));

	summary->version = record->version;

	/* We may not worry, as we are setting a new standard here */
#if 0
	/* Legacy version check, before version 12 we have no upgrade knowledge */
	if ((summary->version > 0xff) && (summary->version & 0xff) < 12) {
		io (printf ("Summary header version mismatch"));
		errno = EINVAL;
		return FALSE;
	}

	if (!(summary->version < 0x100 && summary->version >= 13))
		io (printf ("Loading legacy summary\n"));
	else
		io (printf ("loading new-format summary\n"));
#endif

	summary->flags = record->flags;
	summary->priv->nextuid = record->nextuid;
	summary->time = record->time;
	summary->priv->saved_count = record->saved_count;

	summary->priv->unread_count = record->unread_count;
	summary->priv->deleted_count = record->deleted_count;
	summary->priv->junk_count = record->junk_count;
	summary->priv->visible_count = record->visible_count;
	summary->priv->junk_not_deleted_count = record->jnd_count;

	return TRUE;
}

static	CamelFIRecord *
summary_header_to_db (CamelFolderSummary *summary,
                      GError **error)
{
	CamelFIRecord * record = g_new0 (CamelFIRecord, 1);
	CamelStore *parent_store;
	CamelDB *db;
	const gchar *table_name;

	/* Though we are going to read, we do this during write,
	 * so lets use it that way. */
	table_name = camel_folder_get_full_name (summary->priv->folder);
	parent_store = camel_folder_get_parent_store (summary->priv->folder);
	db = parent_store->cdb_w;

	io (printf ("Savining header to db\n"));

	record->folder_name = g_strdup (table_name);

	/* we always write out the current version */
	record->version = CAMEL_FOLDER_SUMMARY_VERSION;
	record->flags = summary->flags;
	record->nextuid = summary->priv->nextuid;
	record->time = summary->time;

	if (!is_in_memory_summary (summary)) {
		/* FIXME: Ever heard of Constructors and initializing ? */
		if (camel_db_count_total_message_info (db, table_name, &(record->saved_count), NULL))
			record->saved_count = 0;
		if (camel_db_count_junk_message_info (db, table_name, &(record->junk_count), NULL))
			record->junk_count = 0;
		if (camel_db_count_deleted_message_info (db, table_name, &(record->deleted_count), NULL))
			record->deleted_count = 0;
		if (camel_db_count_unread_message_info (db, table_name, &(record->unread_count), NULL))
			record->unread_count = 0;
		if (camel_db_count_visible_message_info (db, table_name, &(record->visible_count), NULL))
			record->visible_count = 0;
		if (camel_db_count_junk_not_deleted_message_info (db, table_name, &(record->jnd_count), NULL))
			record->jnd_count = 0;
	}

	summary->priv->unread_count = record->unread_count;
	summary->priv->deleted_count = record->deleted_count;
	summary->priv->junk_count = record->junk_count;
	summary->priv->visible_count = record->visible_count;
	summary->priv->junk_not_deleted_count = record->jnd_count;

	return record;
}

static CamelMessageInfo *
message_info_from_db (CamelFolderSummary *summary,
                      CamelMIRecord *record)
{
	CamelMessageInfoBase *mi;
	gint i;
	gint count;
	gchar *part, *label;

	mi = (CamelMessageInfoBase *) camel_message_info_new (summary);

	io (printf ("Loading message info from db\n"));

	mi->flags = record->flags;
	mi->size = record->size;
	mi->date_sent = record->dsent;
	mi->date_received = record->dreceived;

	mi->uid = (gchar *) camel_pstring_strdup (record->uid);
	mi->subject = (gchar *) camel_pstring_add (record->subject, FALSE);
	mi->from = (gchar *) camel_pstring_add (record->from, FALSE);
	mi->to = (gchar *) camel_pstring_add (record->to, FALSE);
	mi->cc = (gchar *) camel_pstring_add (record->cc, FALSE);
	mi->mlist = (gchar *) camel_pstring_add (record->mlist, FALSE);

	/* Evolution itself doesn't yet use this, so we ignore it (saving some memory) */
	mi->bodystructure = NULL;

	/* Extract Message id & References */
	mi->content = NULL;
	part = record->part;
	if (part) {
		mi->message_id.id.part.hi = bdata_extract_digit (&part);
		mi->message_id.id.part.lo = bdata_extract_digit (&part);
		count = bdata_extract_digit (&part);

		if (count > 0) {
			mi->references = g_malloc (sizeof (*mi->references) + ((count - 1) * sizeof (mi->references->references[0])));
			mi->references->size = count;
			for (i = 0; i < count; i++) {
				mi->references->references[i].id.part.hi = bdata_extract_digit (&part);
				mi->references->references[i].id.part.lo = bdata_extract_digit (&part);
			}
		} else
			mi->references = NULL;

	}

	/* Extract User flags/labels */
	part = record->labels;
	if (part) {
		label = part;
		for (i = 0; part[i]; i++) {

			if (part[i] == ' ') {
				part[i] = 0;
				camel_flag_set (&mi->user_flags, label, TRUE);
				label = &(part[i + 1]);
			}
		}
		camel_flag_set (&mi->user_flags, label, TRUE);
	}

	/* Extract User tags */
	part = record->usertags;
	count = bdata_extract_digit (&part);
	for (i = 0; i < count; i++) {
		gchar *name, *value;

		name = bdata_extract_string (&part);
		value = bdata_extract_string (&part);
		camel_tag_set (&mi->user_tags, name, value);

		g_free (name);
		g_free (value);
	}

	return (CamelMessageInfo *) mi;
}

static CamelMIRecord *
message_info_to_db (CamelFolderSummary *summary,
                    CamelMessageInfo *info)
{
	CamelMIRecord *record = g_new0 (CamelMIRecord, 1);
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *) info;
	GString *tmp;
	CamelFlag *flag;
	CamelTag *tag;
	gint count, i;

	/* Assume that we dont have to take care of DB Safeness. It will be done while doing the DB transaction */
	record->uid = (gchar *) camel_pstring_strdup (camel_message_info_uid (mi));
	record->flags = mi->flags;

	record->read = ((mi->flags & (CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_JUNK))) ? 1 : 0;
	record->deleted = mi->flags & CAMEL_MESSAGE_DELETED ? 1 : 0;
	record->replied = mi->flags & CAMEL_MESSAGE_ANSWERED ? 1 : 0;
	record->important = mi->flags & CAMEL_MESSAGE_FLAGGED ? 1 : 0;
	record->junk = mi->flags & CAMEL_MESSAGE_JUNK ? 1 : 0;
	record->dirty = mi->flags & CAMEL_MESSAGE_FOLDER_FLAGGED ? 1 : 0;
	record->attachment = mi->flags & CAMEL_MESSAGE_ATTACHMENTS ? 1 : 0;

	record->size = mi->size;
	record->dsent = mi->date_sent;
	record->dreceived = mi->date_received;

	record->subject = (gchar *) camel_pstring_strdup (camel_message_info_subject (mi));
	record->from = (gchar *) camel_pstring_strdup (camel_message_info_from (mi));
	record->to = (gchar *) camel_pstring_strdup (camel_message_info_to (mi));
	record->cc = (gchar *) camel_pstring_strdup (camel_message_info_cc (mi));
	record->mlist = (gchar *) camel_pstring_strdup (camel_message_info_mlist (mi));

	record->followup_flag = (gchar *) camel_pstring_strdup (camel_message_info_user_tag (info, "follow-up"));
	record->followup_completed_on = (gchar *) camel_pstring_strdup (camel_message_info_user_tag (info, "completed-on"));
	record->followup_due_by = (gchar *) camel_pstring_strdup (camel_message_info_user_tag (info, "due-by"));

	record->bodystructure = mi->bodystructure ? g_strdup (mi->bodystructure) : NULL;

	tmp = g_string_new (NULL);
	if (mi->references) {
		g_string_append_printf (tmp, "%lu %lu %lu", (gulong) mi->message_id.id.part.hi, (gulong) mi->message_id.id.part.lo, (gulong) mi->references->size);
		for (i = 0; i < mi->references->size; i++)
			g_string_append_printf (tmp, " %lu %lu", (gulong) mi->references->references[i].id.part.hi, (gulong) mi->references->references[i].id.part.lo);
	} else {
		g_string_append_printf (tmp, "%lu %lu %lu", (gulong) mi->message_id.id.part.hi, (gulong) mi->message_id.id.part.lo, (gulong) 0);
	}
	record->part = tmp->str;
	g_string_free (tmp, FALSE);

	tmp = g_string_new (NULL);
	flag = mi->user_flags;
	while (flag) {
		g_string_append_printf (tmp, "%s ", flag->name);
		flag = flag->next;
	}

	/* Strip off the last space */
	if (tmp->len)
		tmp->len--;

	record->labels = tmp->str;
	g_string_free (tmp, FALSE);

	tmp = g_string_new (NULL);
	count = camel_tag_list_size (&mi->user_tags);
	g_string_append_printf (tmp, "%lu", (gulong) count);
	tag = mi->user_tags;
	while (tag) {
		/* FIXME: Should we handle empty tags? Can it be empty? If it potential crasher ahead*/
		g_string_append_printf (tmp, " %lu-%s %lu-%s", (gulong) strlen (tag->name), tag->name, (gulong) strlen (tag->value), tag->value);
		tag = tag->next;
	}
	record->usertags = tmp->str;
	g_string_free (tmp, FALSE);

	return record;
}

static CamelMessageContentInfo *
content_info_from_db (CamelFolderSummary *summary,
                      CamelMIRecord *record)
{
	CamelMessageContentInfo *ci;
	gchar *type, *subtype;
	guint32 count, i;
	CamelContentType *ct;
	gchar *part = record->cinfo;

	io (printf ("Loading content info from db\n"));

	if (!part)
		return NULL;

	ci = camel_folder_summary_content_info_new (summary);
	if (*part == ' ') part++; /* Move off the space in the record */

	type = bdata_extract_string (&part);
	subtype = bdata_extract_string (&part);
	ct = camel_content_type_new (type, subtype);
	g_free (type);		/* can this be removed? */
	g_free (subtype);
	count = bdata_extract_digit (&part);

	for (i = 0; i < count; i++) {
		gchar *name, *value;
		name = bdata_extract_string (&part);
		value = bdata_extract_string (&part);

		camel_content_type_set_param (ct, name, value);
		/* TODO: do this so we dont have to double alloc/free */
		g_free (name);
		g_free (value);
	}
	ci->type = ct;

	/* FIXME[disk-summary] move all these to camel pstring */
	ci->id = bdata_extract_string (&part);
	ci->description = bdata_extract_string (&part);
	ci->encoding = bdata_extract_string (&part);
	ci->size = bdata_extract_digit (&part);

	record->cinfo = part; /* Keep moving the cursor in the record */

	ci->childs = NULL;

	return ci;
}

static gboolean
content_info_to_db (CamelFolderSummary *summary,
                    CamelMessageContentInfo *ci,
                    CamelMIRecord *record)
{
	CamelContentType *ct;
	struct _camel_header_param *hp;
	GString *str = g_string_new (NULL);
	gchar *oldr;

	io (printf ("Saving content info to db\n"));

	ct = ci->type;
	if (ct) {
		if (ct->type)
			g_string_append_printf (str, " %d-%s", (gint) strlen (ct->type), ct->type);
		else
			g_string_append_printf (str, " 0-");
		if (ct->subtype)
			g_string_append_printf (str, " %d-%s", (gint) strlen (ct->subtype), ct->subtype);
		else
			g_string_append_printf (str, " 0-");
		g_string_append_printf (str, " %d", my_list_size ((struct _node **) &ct->params));
		hp = ct->params;
		while (hp) {
			if (hp->name)
				g_string_append_printf (str, " %d-%s", (gint) strlen (hp->name), hp->name);
			else
				g_string_append_printf (str, " 0-");
			if (hp->value)
				g_string_append_printf (str, " %d-%s", (gint) strlen (hp->value), hp->value);
			else
				g_string_append_printf (str, " 0-");
			hp = hp->next;
		}
	} else {
		g_string_append_printf (str, " %d-", 0);
		g_string_append_printf (str, " %d-", 0);
		g_string_append_printf (str, " %d", 0);
	}

	if (ci->id)
		g_string_append_printf (str, " %d-%s", (gint) strlen (ci->id), ci->id);
	else
		g_string_append_printf (str, " 0-");
	if (ci->description)
		g_string_append_printf (str, " %d-%s", (gint) strlen (ci->description), ci->description);
	else
		g_string_append_printf (str, " 0-");
	if (ci->encoding)
		g_string_append_printf (str, " %d-%s", (gint) strlen (ci->encoding), ci->encoding);
	else
		g_string_append_printf (str, " 0-");
	g_string_append_printf (str, " %u", ci->size);

	if (record->cinfo) {
		oldr = record->cinfo;
		record->cinfo = g_strconcat (oldr, str->str, NULL);
		g_free (oldr); g_string_free (str, TRUE);
	} else {
		record->cinfo = str->str;
		g_string_free (str, FALSE);
	}

	return TRUE;
}

/**
 * camel_folder_summary_replace_flags:
 * @summary: a #CamelFolderSummary
 * @info: a #CamelMessageInfo
 *
 * Updates internal counts based on the flags in @info.
 *
 * Returns: Whether any count changed
 *
 * Since: 3.6
 **/
gboolean
camel_folder_summary_replace_flags (CamelFolderSummary *summary,
                                    CamelMessageInfo *info)
{
	guint32 old_flags, new_flags, added_flags, removed_flags;
	gboolean is_junk_folder = FALSE, is_trash_folder = FALSE;
	GObject *summary_object;
	gboolean changed = FALSE;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);
	g_return_val_if_fail (info != NULL, FALSE);

	if (!camel_message_info_uid (info) ||
	    !camel_folder_summary_check_uid (summary, camel_message_info_uid (info)))
		return FALSE;

	summary_object = G_OBJECT (summary);

	camel_folder_summary_lock (summary);
	g_object_freeze_notify (summary_object);

	old_flags = GPOINTER_TO_UINT (g_hash_table_lookup (summary->priv->uids, camel_message_info_uid (info)));
	new_flags = camel_message_info_flags (info);

	if ((old_flags & ~CAMEL_MESSAGE_FOLDER_FLAGGED) == (new_flags & ~CAMEL_MESSAGE_FOLDER_FLAGGED)) {
		g_object_thaw_notify (summary_object);
		camel_folder_summary_unlock (summary);
		return FALSE;
	}

	if (summary->priv->folder && CAMEL_IS_VTRASH_FOLDER (summary->priv->folder)) {
		CamelVTrashFolder *vtrash = CAMEL_VTRASH_FOLDER (summary->priv->folder);

		is_junk_folder = vtrash && vtrash->type == CAMEL_VTRASH_FOLDER_JUNK;
		is_trash_folder = vtrash && vtrash->type == CAMEL_VTRASH_FOLDER_TRASH;
	}

	added_flags = new_flags & (~(old_flags & new_flags));
	removed_flags = old_flags & (~(old_flags & new_flags));

	if ((old_flags & CAMEL_MESSAGE_SEEN) == (new_flags & CAMEL_MESSAGE_SEEN)) {
		/* unread count is different from others, it asks for nonexistence
		 * of the flag, thus if it wasn't changed, then simply set it
		 * in added/removed, thus there are no false notifications
		 * on unread counts */
		added_flags |= CAMEL_MESSAGE_SEEN;
		removed_flags |= CAMEL_MESSAGE_SEEN;
	} else if ((!is_junk_folder && (new_flags & CAMEL_MESSAGE_JUNK) != 0 &&
		   (old_flags & CAMEL_MESSAGE_JUNK) == (new_flags & CAMEL_MESSAGE_JUNK)) ||
		   (!is_trash_folder && (new_flags & CAMEL_MESSAGE_DELETED) != 0 &&
		   (old_flags & CAMEL_MESSAGE_DELETED) == (new_flags & CAMEL_MESSAGE_DELETED))) {
		/* The message was set read or unread, but it is a junk or deleted message,
		 * in a non-Junk/non-Trash folder, thus it doesn't influence an unread count
		 * there, thus pretend unread didn't change */
		added_flags |= CAMEL_MESSAGE_SEEN;
		removed_flags |= CAMEL_MESSAGE_SEEN;
	}

	/* decrement counts with removed flags */
	changed = folder_summary_update_counts_by_flags (summary, removed_flags, UPDATE_COUNTS_SUB_WITHOUT_TOTAL) || changed;
	/* increment counts with added flags */
	changed = folder_summary_update_counts_by_flags (summary, added_flags, UPDATE_COUNTS_ADD_WITHOUT_TOTAL) || changed;

	/* update current flags on the summary */
	g_hash_table_insert (
		summary->priv->uids,
		(gpointer) camel_pstring_strdup (camel_message_info_uid (info)),
		GUINT_TO_POINTER (new_flags));

	g_object_thaw_notify (summary_object);
	camel_folder_summary_unlock (summary);

	return changed;
}

static CamelMessageInfo *
message_info_clone (CamelFolderSummary *summary,
                    const CamelMessageInfo *mi)
{
	CamelMessageInfoBase *to, *from = (CamelMessageInfoBase *) mi;
	CamelFlag *flag;
	CamelTag *tag;

	to = (CamelMessageInfoBase *) camel_message_info_new (summary);

	to->flags = from->flags;
	to->size = from->size;
	to->date_sent = from->date_sent;
	to->date_received = from->date_received;
	to->refcount = 1;

	/* NB: We don't clone the uid */

	to->subject = camel_pstring_strdup (from->subject);
	to->from = camel_pstring_strdup (from->from);
	to->to = camel_pstring_strdup (from->to);
	to->cc = camel_pstring_strdup (from->cc);
	to->mlist = camel_pstring_strdup (from->mlist);
	memcpy (&to->message_id, &from->message_id, sizeof (to->message_id));
	to->preview = g_strdup (from->preview);
	if (from->references) {
		gint len = sizeof (*from->references) + ((from->references->size - 1) * sizeof (from->references->references[0]));

		to->references = g_malloc (len);
		memcpy (to->references, from->references, len);
	}

	flag = from->user_flags;
	while (flag) {
		camel_flag_set (&to->user_flags, flag->name, TRUE);
		flag = flag->next;
	}

	tag = from->user_tags;
	while (tag) {
		camel_tag_set (&to->user_tags, tag->name, tag->value);
		tag = tag->next;
	}

	if (from->content) {
		/* FIXME: copy content-infos */
	}

	return (CamelMessageInfo *) to;
}

static gconstpointer
info_ptr (const CamelMessageInfo *mi,
          gint id)
{
	switch (id) {
		case CAMEL_MESSAGE_INFO_SUBJECT:
			return ((const CamelMessageInfoBase *) mi)->subject;
		case CAMEL_MESSAGE_INFO_FROM:
			return ((const CamelMessageInfoBase *) mi)->from;
		case CAMEL_MESSAGE_INFO_TO:
			return ((const CamelMessageInfoBase *) mi)->to;
		case CAMEL_MESSAGE_INFO_CC:
			return ((const CamelMessageInfoBase *) mi)->cc;
		case CAMEL_MESSAGE_INFO_MLIST:
			return ((const CamelMessageInfoBase *) mi)->mlist;
		case CAMEL_MESSAGE_INFO_MESSAGE_ID:
			return &((const CamelMessageInfoBase *) mi)->message_id;
		case CAMEL_MESSAGE_INFO_REFERENCES:
			return ((const CamelMessageInfoBase *) mi)->references;
		case CAMEL_MESSAGE_INFO_USER_FLAGS:
			return ((const CamelMessageInfoBase *) mi)->user_flags;
		case CAMEL_MESSAGE_INFO_USER_TAGS:
			return ((const CamelMessageInfoBase *) mi)->user_tags;
		case CAMEL_MESSAGE_INFO_HEADERS:
			return ((const CamelMessageInfoBase *) mi)->headers;
		case CAMEL_MESSAGE_INFO_CONTENT:
			return ((const CamelMessageInfoBase *) mi)->content;
		case CAMEL_MESSAGE_INFO_PREVIEW:
			return ((const CamelMessageInfoBase *) mi)->preview;
		default:
			g_return_val_if_reached (NULL);
	}
}

static guint32
info_uint32 (const CamelMessageInfo *mi,
             gint id)
{
	switch (id) {
		case CAMEL_MESSAGE_INFO_FLAGS:
			return ((const CamelMessageInfoBase *) mi)->flags;
		case CAMEL_MESSAGE_INFO_SIZE:
			return ((const CamelMessageInfoBase *) mi)->size;
		default:
			g_return_val_if_reached (0);
	}
}

static time_t
info_time (const CamelMessageInfo *mi,
           gint id)
{
	switch (id) {
		case CAMEL_MESSAGE_INFO_DATE_SENT:
			return ((const CamelMessageInfoBase *) mi)->date_sent;
		case CAMEL_MESSAGE_INFO_DATE_RECEIVED:
			return ((const CamelMessageInfoBase *) mi)->date_received;
		default:
			g_return_val_if_reached (0);
	}
}

static gboolean
info_user_flag (const CamelMessageInfo *mi,
                const gchar *id)
{
	return camel_flag_get (&((CamelMessageInfoBase *) mi)->user_flags, id);
}

static const gchar *
info_user_tag (const CamelMessageInfo *mi,
               const gchar *id)
{
	return camel_tag_get (&((CamelMessageInfoBase *) mi)->user_tags, id);
}

static gboolean
info_set_user_flag (CamelMessageInfo *info,
                    const gchar *name,
                    gboolean value)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *) info;
	gint res;

	res = camel_flag_set (&mi->user_flags, name, value);

	if (mi->summary && res && mi->summary->priv->folder && mi->uid
	    && camel_folder_summary_check_uid (mi->summary, mi->uid)) {
		CamelFolderChangeInfo *changes = camel_folder_change_info_new ();

		mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		mi->dirty = TRUE;
		camel_folder_summary_touch (mi->summary);
		camel_folder_change_info_change_uid (changes, camel_message_info_uid (info));
		camel_folder_changed (mi->summary->priv->folder, changes);
		camel_folder_change_info_free (changes);
	}

	return res;
}

static gboolean
info_set_user_tag (CamelMessageInfo *info,
                   const gchar *name,
                   const gchar *value)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *) info;
	gint res;

	res = camel_tag_set (&mi->user_tags, name, value);

	if (mi->summary && res && mi->summary->priv->folder && mi->uid
	    && camel_folder_summary_check_uid (mi->summary, mi->uid)) {
		CamelFolderChangeInfo *changes = camel_folder_change_info_new ();

		mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		mi->dirty = TRUE;
		camel_folder_summary_touch (mi->summary);
		camel_folder_change_info_change_uid (changes, camel_message_info_uid (info));
		camel_folder_changed (mi->summary->priv->folder, changes);
		camel_folder_change_info_free (changes);
	}

	return res;
}

static gboolean
info_set_flags (CamelMessageInfo *info,
                guint32 flags,
                guint32 set)
{
	guint32 old;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *) info;
	gboolean counts_changed = FALSE;

	old = camel_message_info_flags (info);
	mi->flags = (old & ~flags) | (set & flags);
	if (old != mi->flags) {
		mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		mi->dirty = TRUE;
		if (mi->summary)
			camel_folder_summary_touch (mi->summary);
	}

	if (mi->summary) {
		camel_folder_summary_lock (mi->summary);
		g_object_freeze_notify (G_OBJECT (mi->summary));
		counts_changed = camel_folder_summary_replace_flags (mi->summary, info);
	}

	if (!counts_changed && ((old & ~CAMEL_MESSAGE_SYSTEM_MASK) == (mi->flags & ~CAMEL_MESSAGE_SYSTEM_MASK)) && !((set & CAMEL_MESSAGE_JUNK_LEARN) && !(set & CAMEL_MESSAGE_JUNK))) {
		if (mi->summary) {
			g_object_thaw_notify (G_OBJECT (mi->summary));
			camel_folder_summary_unlock (mi->summary);
		}
		return FALSE;
	}

	if (mi->summary) {
		g_object_thaw_notify (G_OBJECT (mi->summary));
		camel_folder_summary_unlock (mi->summary);
	}

	if (mi->summary && mi->summary->priv->folder && mi->uid) {
		CamelFolderChangeInfo *changes = camel_folder_change_info_new ();

		camel_folder_change_info_change_uid (changes, camel_message_info_uid (info));
		camel_folder_changed (mi->summary->priv->folder, changes);
		camel_folder_change_info_free (changes);
	}

	return TRUE;
}

static void
camel_folder_summary_class_init (CamelFolderSummaryClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelFolderSummaryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = folder_summary_set_property;
	object_class->get_property = folder_summary_get_property;
	object_class->dispose = folder_summary_dispose;
	object_class->finalize = folder_summary_finalize;

	class->message_info_size = sizeof (CamelMessageInfoBase);
	class->content_info_size = sizeof (CamelMessageContentInfo);

	class->summary_header_from_db = summary_header_from_db;
	class->summary_header_to_db = summary_header_to_db;
	class->message_info_from_db = message_info_from_db;
	class->message_info_to_db = message_info_to_db;
	class->content_info_from_db = content_info_from_db;
	class->content_info_to_db = content_info_to_db;

	class->message_info_new_from_header = message_info_new_from_header;
	class->message_info_new_from_parser = message_info_new_from_parser;
	class->message_info_new_from_message = message_info_new_from_message;
	class->message_info_free = message_info_free;
	class->message_info_clone = message_info_clone;
	class->message_info_from_uid = message_info_from_uid;

	class->content_info_new_from_header = content_info_new_from_header;
	class->content_info_new_from_parser = content_info_new_from_parser;
	class->content_info_new_from_message = content_info_new_from_message;
	class->content_info_free = content_info_free;

	class->next_uid_string = next_uid_string;

	class->info_ptr = info_ptr;
	class->info_uint32 = info_uint32;
	class->info_time = info_time;
	class->info_user_flag = info_user_flag;
	class->info_user_tag = info_user_tag;

	class->info_set_user_flag = info_set_user_flag;
	class->info_set_user_tag = info_set_user_tag;

	class->info_set_flags = info_set_flags;

	/**
	 * CamelFolderSummary:folder
	 *
	 * The #CamelFolder to which the folder summary belongs.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_FOLDER,
		g_param_spec_object (
			"folder",
			"Folder",
			"The folder to which the folder summary belongs",
			CAMEL_TYPE_FOLDER,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	/**
	 * CamelFolderSummary:saved-count
	 *
	 * How many infos is saved in a summary.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_SAVED_COUNT,
		g_param_spec_uint (
			"saved-count",
			"Saved count",
			"How many infos is savef in a summary",
			0,  G_MAXUINT32,
			0, G_PARAM_READABLE));

	/**
	 * CamelFolderSummary:unread-count
	 *
	 * How many unread infos is saved in a summary.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_UNREAD_COUNT,
		g_param_spec_uint (
			"unread-count",
			"Unread count",
			"How many unread infos is saved in a summary",
			0,  G_MAXUINT32,
			0, G_PARAM_READABLE));

	/**
	 * CamelFolderSummary:deleted-count
	 *
	 * How many deleted infos is saved in a summary.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_DELETED_COUNT,
		g_param_spec_uint (
			"deleted-count",
			"Deleted count",
			"How many deleted infos is saved in a summary",
			0,  G_MAXUINT32,
			0, G_PARAM_READABLE));

	/**
	 * CamelFolderSummary:junk-count
	 *
	 * How many junk infos is saved in a summary.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_JUNK_COUNT,
		g_param_spec_uint (
			"junk-count",
			"Junk count",
			"How many junk infos is saved in a summary",
			0,  G_MAXUINT32,
			0, G_PARAM_READABLE));

	/**
	 * CamelFolderSummary:junk-not-deleted-count
	 *
	 * How many junk and not deleted infos is saved in a summary.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_JUNK_NOT_DELETED_COUNT,
		g_param_spec_uint (
			"junk-not-deleted-count",
			"Junk not deleted count",
			"How many junk and not deleted infos is saved in a summary",
			0,  G_MAXUINT32,
			0, G_PARAM_READABLE));

	/**
	 * CamelFolderSummary:visible-count
	 *
	 * How many visible (not deleted and not junk) infos is saved in a summary.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_VISIBLE_COUNT,
		g_param_spec_uint (
			"visible-count",
			"Visible count",
			"How many visible (not deleted and not junk) infos is saved in a summary",
			0,  G_MAXUINT32,
			0, G_PARAM_READABLE));

	/**
	 * CamelFolderSummary:build-content
	 *
	 * Whether to build CamelMessageInfo.content.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_BUILD_CONTENT,
		g_param_spec_boolean (
			"build-content",
			"Build content",
			"Whether to build CamelMessageInfo.content",
			FALSE,
			G_PARAM_READWRITE));

	/**
	 * CamelFolderSummary:need-preview
	 *
	 **/
	g_object_class_install_property (
		object_class,
		PROP_NEED_PREVIEW,
		g_param_spec_boolean (
			"need-preview",
			"Need preview",
			"",
			FALSE,
			G_PARAM_READWRITE));
}

static void
camel_folder_summary_init (CamelFolderSummary *summary)
{
	summary->priv = CAMEL_FOLDER_SUMMARY_GET_PRIVATE (summary);

	summary->version = CAMEL_FOLDER_SUMMARY_VERSION;
	summary->flags = 0;
	summary->time = 0;

	summary->priv->filter_charset = g_hash_table_new (
		camel_strcase_hash, camel_strcase_equal);

	summary->priv->need_preview = FALSE;
	summary->priv->preview_updates = g_hash_table_new (g_str_hash, g_str_equal);

	summary->priv->nextuid = 1;
	summary->priv->uids = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) camel_pstring_free, NULL);
	summary->priv->loaded_infos = g_hash_table_new (g_str_hash, g_str_equal);

	g_rec_mutex_init (&summary->priv->summary_lock);
	g_rec_mutex_init (&summary->priv->filter_lock);

	summary->priv->cache_load_time = 0;
	summary->priv->timeout_handle = 0;
}

/**
 * camel_folder_summary_new:
 * @folder: parent #CamelFolder object
 *
 * Create a new #CamelFolderSummary object.
 *
 * Returns: a new #CamelFolderSummary object
 **/
CamelFolderSummary *
camel_folder_summary_new (CamelFolder *folder)
{
	return g_object_new (CAMEL_TYPE_FOLDER_SUMMARY, "folder", folder, NULL);
}

/**
 * camel_folder_summary_get_index:
 * @summary: a #CamelFolderSummary object
 *
 * Returns: a #CamelFolder to which the summary if associated.
 *
 * Since: 3.4
 **/
CamelFolder *
camel_folder_summary_get_folder (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);

	return summary->priv->folder;
}

/**
 * camel_folder_summary_get_saved_count:
 * @summary: a #CamelFolderSummary object
 *
 * Returns: Count of saved infos.
 *
 * Since: 3.4
 **/
guint32
camel_folder_summary_get_saved_count (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), 0);

	return summary->priv->saved_count;
}

/**
 * camel_folder_summary_get_unread_count:
 * @summary: a #CamelFolderSummary object
 *
 * Returns: Count of unread infos.
 *
 * Since: 3.4
 **/
guint32
camel_folder_summary_get_unread_count (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), 0);

	return summary->priv->unread_count;
}

/**
 * camel_folder_summary_get_deleted_count:
 * @summary: a #CamelFolderSummary object
 *
 * Returns: Count of deleted infos.
 *
 * Since: 3.4
 **/
guint32
camel_folder_summary_get_deleted_count (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), 0);

	return summary->priv->deleted_count;
}

/**
 * camel_folder_summary_get_junk_count:
 * @summary: a #CamelFolderSummary object
 *
 * Returns: Count of junk infos.
 *
 * Since: 3.4
 **/
guint32
camel_folder_summary_get_junk_count (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), 0);

	return summary->priv->junk_count;
}

/**
 * camel_folder_summary_get_junk_not_deleted_count:
 * @summary: a #CamelFolderSummary object
 *
 * Returns: Count of junk and not deleted infos.
 *
 * Since: 3.4
 **/
guint32
camel_folder_summary_get_junk_not_deleted_count (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), 0);

	return summary->priv->junk_not_deleted_count;
}

/**
 * camel_folder_summary_get_visible_count:
 * @summary: a #CamelFolderSummary object
 *
 * Returns: Count of visible (not junk and not deleted) infos.
 *
 * Since: 3.4
 **/
guint32
camel_folder_summary_get_visible_count (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), 0);

	return summary->priv->visible_count;
}

/**
 * camel_folder_summary_set_index:
 * @summary: a #CamelFolderSummary object
 * @index: a #CamelIndex
 *
 * Set the index used to index body content.  If the index is %NULL, or
 * not set (the default), no indexing of body content will take place.
 *
 * Unlike earlier behaviour, build_content need not be set to perform indexing.
 **/
void
camel_folder_summary_set_index (CamelFolderSummary *summary,
                                CamelIndex *index)
{
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	if (index != NULL)
		g_object_ref (index);

	if (summary->priv->index != NULL)
		g_object_unref (summary->priv->index);

	summary->priv->index = index;
}

/**
 * camel_folder_summary_get_index:
 * @summary: a #CamelFolderSummary object
 *
 * Returns: a #CamelIndex used to index body content.
 *
 * Since: 3.4
 **/
CamelIndex *
camel_folder_summary_get_index (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);

	return summary->priv->index;
}

/**
 * camel_folder_summary_set_build_content:
 * @summary: a #CamelFolderSummary object
 * @state: to build or not to build the content
 *
 * Set a flag to tell the summary to build the content info summary
 * (#CamelMessageInfo.content).  The default is not to build content
 * info summaries.
 **/
void
camel_folder_summary_set_build_content (CamelFolderSummary *summary,
                                        gboolean state)
{
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	if (summary->priv->build_content == state)
		return;

	summary->priv->build_content = state;

	g_object_notify (G_OBJECT (summary), "build-content");
}

/**
 * camel_folder_summary_get_build_content:
 * @summary: a #CamelFolderSummary object
 *
 * Returns: Whether to build #CamelMessageInfo.content.
 *
 * Since: 3.4
 **/
gboolean
camel_folder_summary_get_build_content (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);

	return summary->priv->build_content;
}

/**
 * camel_folder_summary_set_need_preview:
 *
 * Since: 2.28
 **/
void
camel_folder_summary_set_need_preview (CamelFolderSummary *summary,
                                       gboolean preview)
{
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	summary->priv->need_preview = preview;
}

/**
 * camel_folder_summary_get_need_preview:
 *
 * Since: 2.28
 **/
gboolean
camel_folder_summary_get_need_preview (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);

	return summary->priv->need_preview;
}

/**
 * camel_folder_summary_next_uid:
 * @summary: a #CamelFolderSummary object
 *
 * Generate a new unique uid value as an integer.  This
 * may be used to create a unique sequence of numbers.
 *
 * Returns: the next unique uid value
 **/
guint32
camel_folder_summary_next_uid (CamelFolderSummary *summary)
{
	guint32 uid;

	camel_folder_summary_lock (summary);

	uid = summary->priv->nextuid++;
	camel_folder_summary_touch (summary);

	camel_folder_summary_unlock (summary);

	return uid;
}

/**
 * camel_folder_summary_set_next_uid:
 * @summary: a #CamelFolderSummary object
 * @uid: The next minimum uid to assign.  To avoid clashing
 * uid's, set this to the uid of a given messages + 1.
 *
 * Set the next minimum uid available.  This can be used to
 * ensure new uid's do not clash with existing uid's.
 **/
void
camel_folder_summary_set_next_uid (CamelFolderSummary *summary,
                                   guint32 uid)
{
	camel_folder_summary_lock (summary);

	summary->priv->nextuid = MAX (summary->priv->nextuid, uid);
	camel_folder_summary_touch (summary);

	camel_folder_summary_unlock (summary);
}

/**
 * camel_folder_summary_get_next_uid:
 * @summary: a #CamelFolderSummary object
 *
 * Returns: Next uid currently awaiting for assignment. The difference from
 *    camel_folder_summary_next_uid() is that this function returns actual
 *    value and doesn't increment it before returning.
 *
 * Since: 3.4
 **/
guint32
camel_folder_summary_get_next_uid (CamelFolderSummary *summary)
{
	guint32 res;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), 0);

	camel_folder_summary_lock (summary);

	res = summary->priv->nextuid;

	camel_folder_summary_unlock (summary);

	return res;
}

/**
 * camel_folder_summary_next_uid_string:
 * @summary: a #CamelFolderSummary object
 *
 * Retrieve the next uid, but as a formatted string.
 *
 * Returns: the next uid as an unsigned integer string.
 * This string must be freed by the caller.
 **/
gchar *
camel_folder_summary_next_uid_string (CamelFolderSummary *summary)
{
	CamelFolderSummaryClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);

	class = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary);
	g_return_val_if_fail (class->next_uid_string != NULL, NULL);

	return class->next_uid_string (summary);
}

/**
 * camel_folder_summary_count:
 * @summary: a #CamelFolderSummary object
 *
 * Get the number of summary items stored in this summary.
 *
 * Returns: the number of items in the summary
 **/
guint
camel_folder_summary_count (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), 0);

	return g_hash_table_size (summary->priv->uids);
}

/**
 * camel_folder_summary_check_uid
 * @summary: a #CamelFolderSummary object
 * @uid: a uid
 *
 * Check if the uid is valid. This isn't very efficient, so it shouldn't be called iteratively.
 *
 *
 * Returns: if the uid is present in the summary or not  (%TRUE or %FALSE)
 *
 * Since: 2.24
 **/
gboolean
camel_folder_summary_check_uid (CamelFolderSummary *summary,
                                const gchar *uid)
{
	gboolean ret;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	camel_folder_summary_lock (summary);

	ret = g_hash_table_lookup_extended (summary->priv->uids, uid, NULL, NULL);

	camel_folder_summary_unlock (summary);

	return ret;
}

static void
folder_summary_dupe_uids_to_array (gpointer key_uid,
                                   gpointer value_flags,
                                   gpointer user_data)
{
	g_ptr_array_add (user_data, (gpointer) camel_pstring_strdup (key_uid));
}

/**
 * camel_folder_summary_get_array:
 * @summary: a #CamelFolderSummary object
 *
 * Obtain a copy of the summary array.  This is done atomically,
 * so cannot contain empty entries.
 *
 * Free with camel_folder_summary_free_array()
 *
 * Returns: a #GPtrArray of uids
 *
 * Since: 3.4
 **/
GPtrArray *
camel_folder_summary_get_array (CamelFolderSummary *summary)
{
	GPtrArray *res;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);

	camel_folder_summary_lock (summary);

	res = g_ptr_array_sized_new (g_hash_table_size (summary->priv->uids));
	g_hash_table_foreach (summary->priv->uids, folder_summary_dupe_uids_to_array, res);

	camel_folder_summary_unlock (summary);

	return res;
}

/**
 * camel_folder_summary_free_array:
 * @array: a #GPtrArray returned from camel_folder_summary_get_array()
 *
 * Free's array and its elements returned from camel_folder_summary_get_array().
 *
 * Since: 3.4
 **/
void
camel_folder_summary_free_array (GPtrArray *array)
{
	if (!array)
		return;

	g_ptr_array_foreach (array, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (array, TRUE);
}

static void
cfs_copy_uids_cb (gpointer key,
                  gpointer value,
                  gpointer user_data)
{
	const gchar *uid = key;
	GHashTable *copy_hash = user_data;

	g_hash_table_insert (copy_hash, (gpointer) camel_pstring_strdup (uid), GINT_TO_POINTER (1));
}

/**
 * camel_folder_summary_get_hash:
 * @summary: a #CamelFolderSummary object
 *
 * Returns hash of current stored 'uids' in summary, where key is 'uid'
 * from the string pool, and value is 1. The returned pointer should
 * be freed with g_hash_table_destroy().
 *
 * Note: When searching for values always use uids from the string pool.
 *
 * Since: 3.6
 **/
GHashTable *
camel_folder_summary_get_hash (CamelFolderSummary *summary)
{
	GHashTable *uids;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);

	camel_folder_summary_lock (summary);

	/* using direct hash because of strings being from the string pool */
	uids = g_hash_table_new_full (g_direct_hash, g_direct_equal, (GDestroyNotify) camel_pstring_free, NULL);
	g_hash_table_foreach (summary->priv->uids, cfs_copy_uids_cb, uids);

	camel_folder_summary_unlock (summary);

	return uids;
}

/**
 * camel_folder_summary_peek_loaded:
 *
 * Since: 2.26
 **/
CamelMessageInfo *
camel_folder_summary_peek_loaded (CamelFolderSummary *summary,
                                  const gchar *uid)
{
	CamelMessageInfo *info;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);

	info = g_hash_table_lookup (summary->priv->loaded_infos, uid);

	if (info)
		camel_message_info_ref (info);

	return info;
}

struct _db_pass_data {
	GHashTable *columns_hash;
	CamelFolderSummary *summary;
	gboolean add; /* or just insert to hashtable */
};

static CamelMessageInfo *
message_info_from_uid (CamelFolderSummary *summary,
                       const gchar *uid)
{
	CamelMessageInfo *info;
	gint ret;

	camel_folder_summary_lock (summary);

	info = g_hash_table_lookup (summary->priv->loaded_infos, uid);

	if (!info) {
		CamelDB *cdb;
		CamelStore *parent_store;
		const gchar *folder_name;
		struct _db_pass_data data;

		folder_name = camel_folder_get_full_name (summary->priv->folder);

		if (is_in_memory_summary (summary)) {
			camel_folder_summary_unlock (summary);
			g_warning (
				"%s: Tried to load uid '%s' "
				"from DB on in-memory summary of '%s'",
				G_STRFUNC, uid, folder_name);
			return NULL;
		}

		parent_store =
			camel_folder_get_parent_store (summary->priv->folder);
		cdb = parent_store->cdb_r;

		data.columns_hash = NULL;
		data.summary = summary;
		data.add = FALSE;

		ret = camel_db_read_message_info_record_with_uid (
			cdb, folder_name, uid, &data,
			camel_read_mir_callback, NULL);
		if (data.columns_hash)
			g_hash_table_destroy (data.columns_hash);

		if (ret != 0) {
			camel_folder_summary_unlock (summary);
			return NULL;
		}

		/* We would have double reffed at camel_read_mir_callback */
		info = g_hash_table_lookup (summary->priv->loaded_infos, uid);

		cfs_schedule_info_release_timer (summary);
	}

	if (info)
		camel_message_info_ref (info);

	camel_folder_summary_unlock (summary);

	return info;
}

/**
 * camel_folder_summary_get:
 * @summary: a #CamelFolderSummary object
 * @uid: a uid
 *
 * Retrieve a summary item by uid.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 *
 * Returns: the summary item, or %NULL if the uid @uid is not available
 *
 * See camel_folder_summary_get_info_flags().
 *
 * Since: 3.4
 **/
CamelMessageInfo *
camel_folder_summary_get (CamelFolderSummary *summary,
                          const gchar *uid)
{
	CamelFolderSummaryClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	class = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary);
	g_return_val_if_fail (class->message_info_from_uid != NULL, NULL);

	return class->message_info_from_uid (summary, uid);
}

/**
 * camel_folder_summary_get_info_flags:
 * @summary: a #CamelFolderSummary object
 * @uid: a uid
 *
 * Retrieve CamelMessageInfo::flags for a message info with UID @uid.
 * This is much quicker than camel_folder_summary_get(), because it
 * doesn't require reading the message info from a disk.
 *
 * Returns: the flags currently stored for message info with UID @uid,
 *          or (~0) on error
 *
 * Since: 3.12
 **/
guint32
camel_folder_summary_get_info_flags (CamelFolderSummary *summary,
				     const gchar *uid)
{
	gpointer ptr_uid = NULL, ptr_flags = NULL;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), (~0));
	g_return_val_if_fail (uid != NULL, (~0));

	camel_folder_summary_lock (summary);
	if (!g_hash_table_lookup_extended (summary->priv->uids, uid, &ptr_uid, &ptr_flags)) {
		camel_folder_summary_unlock (summary);
		return (~0);
	}

	camel_folder_summary_unlock (summary);

	return GPOINTER_TO_UINT (ptr_flags);
}

static CamelMessageContentInfo *
perform_content_info_load_from_db (CamelFolderSummary *summary,
                                   CamelMIRecord *mir)
{
	gint i;
	guint32 count;
	CamelMessageContentInfo *ci, *pci;
	gchar *part;

	ci = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->content_info_from_db (summary, mir);
	if (ci == NULL)
		return NULL;
	part = mir->cinfo;
	if (!part)
		return ci;
	if (*part == ' ') part++;
	count = bdata_extract_digit (&part);

	mir->cinfo = part;
	for (i = 0; i < count; i++) {
		pci = perform_content_info_load_from_db (summary, mir);
		if (pci ) {
			my_list_append ((struct _node **) &ci->childs, (struct _node *) pci);
			pci->parent = ci;
		} else {
			d (fprintf (stderr, "Summary file format messed up?"));
			camel_folder_summary_content_info_free (summary, ci);
			return NULL;
		}
	}
	return ci;
}

static void
gather_dirty_uids (gpointer key,
                   gpointer value,
                   gpointer user_data)
{
	const gchar *uid = key;
	CamelMessageInfoBase *info = value;
	GHashTable *hash = user_data;

	if (info->dirty)
		g_hash_table_insert (hash, (gpointer) camel_pstring_strdup (uid), GINT_TO_POINTER (1));
}

static void
gather_changed_uids (gpointer key,
                     gpointer value,
                     gpointer user_data)
{
	const gchar *uid = key;
	guint32 flags = GPOINTER_TO_UINT (value);
	GHashTable *hash = user_data;

	if ((flags & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0)
		g_hash_table_insert (hash, (gpointer) camel_pstring_strdup (uid), GINT_TO_POINTER (1));
}

/**
 * camel_folder_summary_get_changed:
 *
 * Since: 2.24
 **/
GPtrArray *
camel_folder_summary_get_changed (CamelFolderSummary *summary)
{
	GPtrArray *res;
	GHashTable *hash = g_hash_table_new_full (g_direct_hash, g_direct_equal, (GDestroyNotify) camel_pstring_free, NULL);

	camel_folder_summary_lock (summary);

	g_hash_table_foreach (summary->priv->loaded_infos, gather_dirty_uids, hash);
	g_hash_table_foreach (summary->priv->uids, gather_changed_uids, hash);

	res = g_ptr_array_sized_new (g_hash_table_size (hash));
	g_hash_table_foreach (hash, folder_summary_dupe_uids_to_array, res);

	camel_folder_summary_unlock (summary);

	g_hash_table_destroy (hash);

	return res;
}

static void
count_changed_uids (gchar *key,
                    CamelMessageInfoBase *info,
                    gint *count)
{
	if (info->dirty)
		(*count)++;
}

static gint
cfs_count_dirty (CamelFolderSummary *summary)
{
	gint count = 0;

	camel_folder_summary_lock (summary);
	g_hash_table_foreach (summary->priv->loaded_infos, (GHFunc) count_changed_uids, &count);
	camel_folder_summary_unlock (summary);

	return count;
}

static gboolean
remove_item (gchar *uid,
             CamelMessageInfoBase *info,
             GSList **to_remove_infos)
{
	if (info->refcount == 1 && !info->dirty && (info->flags & CAMEL_MESSAGE_FOLDER_FLAGGED) == 0) {
		*to_remove_infos = g_slist_prepend (*to_remove_infos, info);
		return TRUE;
	}

	return FALSE;
}

static void
remove_cache (CamelSession *session,
              GCancellable *cancellable,
              CamelFolderSummary *summary,
              GError **error)
{
	GSList *to_remove_infos = NULL;

	CAMEL_DB_RELEASE_SQLITE_MEMORY;

	if (time (NULL) - summary->priv->cache_load_time < SUMMARY_CACHE_DROP)
		return;

	camel_folder_summary_lock (summary);

	g_hash_table_foreach_remove (summary->priv->loaded_infos, (GHRFunc) remove_item, &to_remove_infos);

	g_slist_foreach (to_remove_infos, (GFunc) camel_message_info_unref, NULL);
	g_slist_free (to_remove_infos);

	camel_folder_summary_unlock (summary);

	summary->priv->cache_load_time = time (NULL);
}

static gboolean
cfs_try_release_memory (CamelFolderSummary *summary)
{
	CamelStore *parent_store;
	CamelSession *session;

	/* If folder is freed or if the cache is nil then clean up */
	if (!summary->priv->folder ||
	    !g_hash_table_size (summary->priv->loaded_infos) ||
	    is_in_memory_summary (summary)) {
		summary->priv->cache_load_time = 0;
		summary->priv->timeout_handle = 0;
		g_object_unref (summary);

		return FALSE;
	}

	if (time (NULL) - summary->priv->cache_load_time < SUMMARY_CACHE_DROP)
		return TRUE;

	parent_store = camel_folder_get_parent_store (summary->priv->folder);
	session = camel_service_ref_session (CAMEL_SERVICE (parent_store));

	camel_session_submit_job (
		session,
		(CamelSessionCallback) remove_cache,
		g_object_ref (summary),
		(GDestroyNotify) g_object_unref);

	g_object_unref (session);

	return TRUE;
}

static void
cfs_schedule_info_release_timer (CamelFolderSummary *summary)
{
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	if (is_in_memory_summary (summary))
		return;

	if (!summary->priv->timeout_handle) {
		static gboolean know_can_do = FALSE, can_do = TRUE;

		if (!know_can_do) {
			can_do = !g_getenv ("CAMEL_FREE_INFOS");
			know_can_do = TRUE;
		}

		/* FIXME[disk-summary] LRU please and not timeouts */
		if (can_do) {
			summary->priv->timeout_handle = g_timeout_add_seconds (
				SUMMARY_CACHE_DROP,
				(GSourceFunc) cfs_try_release_memory,
				g_object_ref (summary));
			g_source_set_name_by_id (
				summary->priv->timeout_handle,
				"[camel] cfs_try_release_memory");
		}
	}

	/* update also cache load time to the actual, to not release something just loaded */
	summary->priv->cache_load_time = time (NULL);
}

static gint
cfs_cache_size (CamelFolderSummary *summary)
{
	/* FIXME[disk-summary] this is a timely hack. fix it well */
	if (!CAMEL_IS_VEE_FOLDER (summary->priv->folder))
		return g_hash_table_size (summary->priv->loaded_infos);
	else
		return g_hash_table_size (summary->priv->uids);
}

/* Update preview of cached messages */

static void
msg_update_preview (const gchar *uid,
                    gpointer value,
                    CamelFolder *folder)
{
	CamelMessageInfoBase *info = (CamelMessageInfoBase *) camel_folder_summary_get (folder->summary, uid);
	CamelMimeMessage *msg;
	CamelStore *parent_store;
	const gchar *full_name;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	/* FIXME Pass a GCancellable */
	msg = camel_folder_get_message_sync (folder, uid, NULL, NULL);
	if (msg != NULL) {
		if (camel_mime_message_build_preview ((CamelMimePart *) msg, (CamelMessageInfo *) info) && info->preview) {
			if (!is_in_memory_summary (folder->summary))
				camel_db_write_preview_record (parent_store->cdb_w, full_name, info->uid, info->preview, NULL);
		}
	}
	camel_message_info_unref (info);
}

static void
pick_uids (const gchar *uid,
           CamelMessageInfoBase *mi,
           GPtrArray *array)
{
	if (mi->preview)
		g_ptr_array_add (array, (gchar *) camel_pstring_strdup (uid));
}

static void
copy_all_uids_to_hash (gpointer uid,
                       gpointer hash)
{
	g_return_if_fail (uid != NULL);

	g_hash_table_insert (hash, (gchar *) camel_pstring_strdup (uid), GINT_TO_POINTER (1));
}

static gboolean
fill_mi (const gchar *uid,
         const gchar *msg,
         CamelFolder *folder)
{
	CamelMessageInfoBase *info;

	info = g_hash_table_lookup (folder->summary->priv->loaded_infos, uid);
	if (info) /* We re assign the memory of msg */
		info->preview = (gchar *) msg;
	camel_pstring_free (uid); /* unref the uid */

	return TRUE;
}

static void
preview_update (CamelSession *session,
                GCancellable *cancellable,
                CamelFolder *folder,
                GError **error)
{
	/* FIXME: Either lock & use or copy & use.*/
	GPtrArray *uids_uncached, *uids_array;
	GHashTable *preview_data, *uids_hash;
	CamelStore *parent_store;
	const gchar *full_name;
	gboolean is_in_memory = is_in_memory_summary (folder->summary);
	gint i;

	uids_array = camel_folder_summary_get_array (folder->summary);
	uids_hash = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) camel_pstring_free, NULL);
	g_ptr_array_foreach (uids_array, copy_all_uids_to_hash, uids_hash);
	uids_uncached = camel_folder_get_uncached_uids (folder, uids_array, NULL);
	camel_folder_summary_free_array (uids_array);
	uids_array = NULL;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	preview_data = is_in_memory ? NULL : camel_db_get_folder_preview (parent_store->cdb_r, full_name, NULL);
	if (preview_data) {
		g_hash_table_foreach_remove (preview_data, (GHRFunc) fill_mi, folder);
		g_hash_table_destroy (preview_data);
	}

	camel_folder_summary_lock (folder->summary);
	g_hash_table_foreach (folder->summary->priv->loaded_infos, (GHFunc) pick_uids, uids_uncached);
	camel_folder_summary_unlock (folder->summary);

	for (i = 0; i < uids_uncached->len; i++) {
		g_hash_table_remove (uids_hash, uids_uncached->pdata[i]);
	}

	camel_folder_lock (folder);
	if (!is_in_memory)
		camel_db_begin_transaction (parent_store->cdb_w, NULL);
	g_hash_table_foreach (uids_hash, (GHFunc) msg_update_preview, folder);
	if (!is_in_memory)
		camel_db_end_transaction (parent_store->cdb_w, NULL);
	camel_folder_unlock (folder);
	camel_folder_free_uids (folder, uids_uncached);
	g_hash_table_destroy (uids_hash);
}

/* end */

static gint
cfs_reload_from_db (CamelFolderSummary *summary,
                    GError **error)
{
	CamelDB *cdb;
	CamelStore *parent_store;
	const gchar *folder_name;
	gint ret = 0;
	struct _db_pass_data data;

	/* FIXME[disk-summary] baseclass this, and vfolders we may have to
	 * load better. */
	d (printf ("\ncamel_folder_summary_reload_from_db called \n"));

	if (is_in_memory_summary (summary))
		return 0;

	folder_name = camel_folder_get_full_name (summary->priv->folder);
	parent_store = camel_folder_get_parent_store (summary->priv->folder);
	cdb = parent_store->cdb_r;

	data.columns_hash = NULL;
	data.summary = summary;
	data.add = FALSE;

	ret = camel_db_read_message_info_records (
		cdb, folder_name, (gpointer) &data,
		camel_read_mir_callback, NULL);

	if (data.columns_hash)
		g_hash_table_destroy (data.columns_hash);

	cfs_schedule_info_release_timer (summary);

	/* FIXME Convert this to a GTask, submitted through
	 *       camel_service_queue_task().  Then it won't
	 *       have to call camel_folder_lock/unlock(). */
	if (summary->priv->need_preview) {
		CamelSession *session;

		/* This may not be available in a case of this being called as part
		   of CamelSession's dispose, because the CamelService uses GWeakRef
		   object which is invalidates its content when it reaches the dispose. */
		session = camel_service_ref_session (CAMEL_SERVICE (parent_store));
		if (session) {
			camel_session_submit_job (
				session,
				(CamelSessionCallback) preview_update,
				g_object_ref (summary->priv->folder),
				(GDestroyNotify) g_object_unref);
			g_object_unref (session);
		}
	}

	return ret == 0 ? 0 : -1;
}

/**
 * camel_folder_summary_add_preview:
 *
 * Since: 2.28
 **/
void
camel_folder_summary_add_preview (CamelFolderSummary *summary,
                                  CamelMessageInfo *info)
{
	camel_folder_summary_lock (summary);
	g_hash_table_insert (summary->priv->preview_updates, (gchar *) info->uid, ((CamelMessageInfoBase *) info)->preview);
	camel_folder_summary_touch (summary);
	camel_folder_summary_unlock (summary);
}

/**
 * camel_folder_summary_prepare_fetch_all:
 * @summary: #CamelFolderSummary object
 * @error: return location for a #GError, or %NULL
 *
 * Loads all infos into memory, if they are not yet and ensures
 * they will not be freed in next couple minutes. Call this function
 * before any mass operation or when all message infos will be needed,
 * for better performance.
 *
 * Since: 2.32
 **/
void
camel_folder_summary_prepare_fetch_all (CamelFolderSummary *summary,
                                        GError **error)
{
	guint loaded, known;

	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	loaded = cfs_cache_size (summary);
	known = camel_folder_summary_count (summary);

	if (known - loaded > 50) {
		camel_folder_summary_lock (summary);
		cfs_reload_from_db (summary, error);
		camel_folder_summary_unlock (summary);
	}

	/* update also cache load time, even when not loaded anything */
	summary->priv->cache_load_time = time (NULL);
}

/**
 * camel_folder_summary_load_from_db:
 *
 * Since: 2.24
 **/
gboolean
camel_folder_summary_load_from_db (CamelFolderSummary *summary,
                                   GError **error)
{
	CamelDB *cdb;
	CamelStore *parent_store;
	const gchar *full_name;
	gint ret = 0;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);

	if (is_in_memory_summary (summary))
		return TRUE;

	camel_folder_summary_lock (summary);
	camel_folder_summary_save_to_db (summary, NULL);

	/* struct _db_pass_data data; */
	d (printf ("\ncamel_folder_summary_load_from_db called \n"));

	full_name = camel_folder_get_full_name (summary->priv->folder);
	parent_store = camel_folder_get_parent_store (summary->priv->folder);
	if (!camel_folder_summary_header_load_from_db (summary, parent_store, full_name, error)) {
		camel_folder_summary_unlock (summary);
		return FALSE;
	}

	cdb = parent_store->cdb_r;

	ret = camel_db_get_folder_uids (
		cdb, full_name, summary->sort_by, summary->collate,
		summary->priv->uids, &local_error);

	if (local_error != NULL && local_error->message != NULL &&
	    strstr (local_error->message, "no such table") != NULL) {
		g_clear_error (&local_error);

		/* create table the first time it is accessed and missing */
		ret = camel_db_prepare_message_info_table (cdb, full_name, error);
	} else if (local_error != NULL)
		g_propagate_error (error, local_error);

	camel_folder_summary_unlock (summary);

	return ret == 0;
}

static void
mir_from_cols (CamelMIRecord *mir,
               CamelFolderSummary *summary,
               GHashTable **columns_hash,
               gint ncol,
               gchar **cols,
               gchar **name)
{
	gint i;

	for (i = 0; i < ncol; ++i) {
		if (!name[i] || !cols[i])
			continue;

		switch (camel_db_get_column_ident (columns_hash, i, ncol, name)) {
			case CAMEL_DB_COLUMN_UID:
				mir->uid = (gchar *) camel_pstring_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_FLAGS:
				mir->flags = cols[i] ? strtoul (cols[i], NULL, 10) : 0;
				break;
			case CAMEL_DB_COLUMN_READ:
				mir->read = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
				break;
			case CAMEL_DB_COLUMN_DELETED:
				mir->deleted = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
				break;
			case CAMEL_DB_COLUMN_REPLIED:
				mir->replied = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
				break;
			case CAMEL_DB_COLUMN_IMPORTANT:
				mir->important = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
				break;
			case CAMEL_DB_COLUMN_JUNK:
				mir->junk = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
				break;
			case CAMEL_DB_COLUMN_ATTACHMENT:
				mir->attachment = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
				break;
			case CAMEL_DB_COLUMN_SIZE:
				mir->size = cols[i] ? strtoul (cols[i], NULL, 10) : 0;
				break;
			case CAMEL_DB_COLUMN_DSENT:
				mir->dsent = cols[i] ? strtol (cols[i], NULL, 10) : 0;
				break;
			case CAMEL_DB_COLUMN_DRECEIVED:
				mir->dreceived = cols[i] ? strtol (cols[i], NULL, 10) : 0;
				break;
			case CAMEL_DB_COLUMN_SUBJECT:
				mir->subject = (gchar *) camel_pstring_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_MAIL_FROM:
				mir->from = (gchar *) camel_pstring_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_MAIL_TO:
				mir->to = (gchar *) camel_pstring_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_MAIL_CC:
				mir->cc = (gchar *) camel_pstring_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_MLIST:
				mir->mlist = (gchar *) camel_pstring_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_FOLLOWUP_FLAG:
				mir->followup_flag = (gchar *) camel_pstring_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_FOLLOWUP_COMPLETED_ON:
				mir->followup_completed_on = (gchar *) camel_pstring_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_FOLLOWUP_DUE_BY:
				mir->followup_due_by = (gchar *) camel_pstring_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_PART:
				mir->part = g_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_LABELS:
				mir->labels = g_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_USERTAGS:
				mir->usertags = g_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_CINFO:
				mir->cinfo = g_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_BDATA:
				mir->bdata = g_strdup (cols[i]);
				break;
			case CAMEL_DB_COLUMN_BODYSTRUCTURE:
				/* Evolution itself doesn't yet use this, ignoring */
				/* mir->bodystructure = g_strdup (cols[i]); */
				break;
			default:
				g_warn_if_reached ();
				break;
		}
	}
}

static gint
camel_read_mir_callback (gpointer ref,
                         gint ncol,
                         gchar **cols,
                         gchar **name)
{
	struct _db_pass_data *data = (struct _db_pass_data *) ref;
	CamelFolderSummary *summary = data->summary;
	CamelMIRecord *mir;
	CamelMessageInfo *info;
	gint ret = 0;

	mir = g_new0 (CamelMIRecord , 1);
	mir_from_cols (mir, summary, &data->columns_hash, ncol, cols, name);

	camel_folder_summary_lock (summary);
	if (!mir->uid || g_hash_table_lookup (summary->priv->loaded_infos, mir->uid)) {
		/* Unlock and better return */
		camel_folder_summary_unlock (summary);
		camel_db_camel_mir_free (mir);
		return ret;
	}
	camel_folder_summary_unlock (summary);

	info = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_from_db (summary, mir);

	if (info) {

		if (summary->priv->build_content) {
			gchar *tmp;
			tmp = mir->cinfo;
			/* FIXME: this should be done differently, how i don't know */
			((CamelMessageInfoBase *) info)->content = perform_content_info_load_from_db (summary, mir);
			if (((CamelMessageInfoBase *) info)->content == NULL) {
				camel_message_info_unref (info);
				info = NULL;
			}
			mir->cinfo = tmp;

			if (!info) {
				camel_db_camel_mir_free (mir);
				return -1;
			}
		}

		/* Just now we are reading from the DB, it can't be dirty. */
		((CamelMessageInfoBase *) info)->dirty = FALSE;
		if (data->add)
			camel_folder_summary_add (summary, info);
		else
			camel_folder_summary_insert (summary, info, TRUE);

	} else {
		g_warning ("Loading messageinfo from db failed");
		ret = -1;
	}

	camel_db_camel_mir_free (mir);

	return ret;
}

/* saves the content descriptions, recursively */
static gboolean
perform_content_info_save_to_db (CamelFolderSummary *summary,
                                 CamelMessageContentInfo *ci,
                                 CamelMIRecord *record)
{
	CamelMessageContentInfo *part;
	gchar *oldr;

	if (!CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->content_info_to_db (summary, ci, record))
		return FALSE;

	oldr = record->cinfo;
	record->cinfo = g_strdup_printf ("%s %d", oldr, my_list_size ((struct _node **) &ci->childs));
	g_free (oldr);

	part = ci->childs;
	while (part) {
		if (perform_content_info_save_to_db (summary, part, record) == -1)
			return FALSE;
		part = part->next;
	}

	return TRUE;
}

static void
save_to_db_cb (gpointer key,
               gpointer value,
               gpointer data)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *) value;
	CamelFolderSummary *summary = (CamelFolderSummary *) mi->summary;
	CamelStore *parent_store;
	const gchar *full_name;
	CamelDB *cdb;
	CamelMIRecord *mir;
	GError **error = data;

	full_name = camel_folder_get_full_name (summary->priv->folder);
	parent_store = camel_folder_get_parent_store (summary->priv->folder);
	cdb = parent_store->cdb_w;

	if (!mi->dirty)
		return;

	mir = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_to_db (summary, (CamelMessageInfo *) mi);

	if (mir && summary->priv->build_content) {
		if (!perform_content_info_save_to_db (summary, ((CamelMessageInfoBase *) mi)->content, mir)) {
			g_warning ("unable to save mir+cinfo for uid: %s\n", mir->uid);
			camel_db_camel_mir_free (mir);
			/* FIXME: Add exception here */
			return;
		}
	}

	g_return_if_fail (mir != NULL);

	if (camel_db_write_message_info_record (cdb, full_name, mir, error) != 0) {
		camel_db_camel_mir_free (mir);
		return;
	}

	/* Reset the dirty flag which decides if the changes are synced to the DB or not.
	The FOLDER_FLAGGED should be used to check if the changes are synced to the server.
	So, dont unset the FOLDER_FLAGGED flag */
	mi->dirty = FALSE;

	camel_db_camel_mir_free (mir);
}

static gint
save_message_infos_to_db (CamelFolderSummary *summary,
                          GError **error)
{
	CamelStore *parent_store;
	CamelDB *cdb;
	const gchar *full_name;

	if (is_in_memory_summary (summary))
		return 0;

	full_name = camel_folder_get_full_name (summary->priv->folder);
	parent_store = camel_folder_get_parent_store (summary->priv->folder);
	cdb = parent_store->cdb_w;

	if (camel_db_prepare_message_info_table (cdb, full_name, error) != 0)
		return -1;

	camel_folder_summary_lock (summary);

	/* Push MessageInfo-es */
	camel_db_begin_transaction (cdb, NULL);
	g_hash_table_foreach (summary->priv->loaded_infos, save_to_db_cb, error);
	camel_db_end_transaction (cdb, NULL);

	camel_folder_summary_unlock (summary);
	cfs_schedule_info_release_timer (summary);

	return 0;
}

static void
msg_save_preview (const gchar *uid,
                  gpointer value,
                  CamelFolder *folder)
{
	CamelStore *parent_store;
	const gchar *full_name;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	camel_db_write_preview_record (
		parent_store->cdb_w, full_name, uid, (gchar *) value, NULL);
}

/**
 * camel_folder_summary_save_to_db:
 *
 * Since: 2.24
 **/
gboolean
camel_folder_summary_save_to_db (CamelFolderSummary *summary,
                                 GError **error)
{
	CamelStore *parent_store;
	CamelDB *cdb;
	CamelFIRecord *record;
	gint ret, count;

	g_return_val_if_fail (summary != NULL, FALSE);

	if (!(summary->flags & CAMEL_FOLDER_SUMMARY_DIRTY) ||
	    is_in_memory_summary (summary))
		return TRUE;

	parent_store = camel_folder_get_parent_store (summary->priv->folder);
	cdb = parent_store->cdb_w;

	camel_folder_summary_lock (summary);

	d (printf ("\ncamel_folder_summary_save_to_db called \n"));
	if (summary->priv->need_preview && g_hash_table_size (summary->priv->preview_updates)) {
		camel_db_begin_transaction (parent_store->cdb_w, NULL);
		g_hash_table_foreach (summary->priv->preview_updates, (GHFunc) msg_save_preview, summary->priv->folder);
		g_hash_table_remove_all (summary->priv->preview_updates);
		camel_db_end_transaction (parent_store->cdb_w, NULL);
	}

	summary->flags &= ~CAMEL_FOLDER_SUMMARY_DIRTY;

	count = cfs_count_dirty (summary);
	if (!count) {
		gboolean res = camel_folder_summary_header_save_to_db (summary, error);
		camel_folder_summary_unlock (summary);
		return res;
	}

	ret = save_message_infos_to_db (summary, error);
	if (ret != 0) {
		/* Failed, so lets reset the flag */
		summary->flags |= CAMEL_FOLDER_SUMMARY_DIRTY;
		camel_folder_summary_unlock (summary);
		return FALSE;
	}

	/* XXX So... if an error is set, how do we even reach this point
	 *     given the above error check?  Oye vey this logic is nasty. */
	if (error != NULL && *error != NULL &&
		strstr ((*error)->message, "26 columns but 28 values") != NULL) {
		const gchar *full_name;

		full_name = camel_folder_get_full_name (summary->priv->folder);
		g_warning ("Fixing up a broken summary migration on %s\n", full_name);

		/* Begin everything again. */
		camel_db_begin_transaction (cdb, NULL);
		camel_db_reset_folder_version (cdb, full_name, 0, NULL);
		camel_db_end_transaction (cdb, NULL);

		ret = save_message_infos_to_db (summary, error);
		if (ret != 0) {
			summary->flags |= CAMEL_FOLDER_SUMMARY_DIRTY;
			camel_folder_summary_unlock (summary);
			return FALSE;
		}
	}

	record = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->summary_header_to_db (summary, error);
	if (!record) {
		summary->flags |= CAMEL_FOLDER_SUMMARY_DIRTY;
		camel_folder_summary_unlock (summary);
		return FALSE;
	}

	camel_db_begin_transaction (cdb, NULL);
	ret = camel_db_write_folder_info_record (cdb, record, error);
	g_free (record->folder_name);
	g_free (record->bdata);
	g_free (record);

	if (ret != 0) {
		camel_db_abort_transaction (cdb, NULL);
		summary->flags |= CAMEL_FOLDER_SUMMARY_DIRTY;
		camel_folder_summary_unlock (summary);
		return FALSE;
	}

	camel_db_end_transaction (cdb, NULL);
	camel_folder_summary_unlock (summary);

	return ret == 0;
}

/**
 * camel_folder_summary_header_save_to_db:
 *
 * Since: 2.24
 **/
gboolean
camel_folder_summary_header_save_to_db (CamelFolderSummary *summary,
                                        GError **error)
{
	CamelStore *parent_store;
	CamelFIRecord *record;
	CamelDB *cdb;
	gint ret;

	if (is_in_memory_summary (summary))
		return TRUE;

	parent_store = camel_folder_get_parent_store (summary->priv->folder);
	cdb = parent_store->cdb_w;
	camel_folder_summary_lock (summary);

	d (printf ("\ncamel_folder_summary_header_save_to_db called \n"));

	record = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->summary_header_to_db (summary, error);
	if (!record) {
		camel_folder_summary_unlock (summary);
		return FALSE;
	}

	camel_db_begin_transaction (cdb, NULL);
	ret = camel_db_write_folder_info_record (cdb, record, error);
	g_free (record->folder_name);
	g_free (record->bdata);
	g_free (record);

	if (ret != 0) {
		camel_db_abort_transaction (cdb, NULL);
		camel_folder_summary_unlock (summary);
		return FALSE;
	}

	camel_db_end_transaction (cdb, NULL);
	camel_folder_summary_unlock (summary);

	return ret == 0;
}

/**
 * camel_folder_summary_header_load_from_db:
 *
 * Since: 2.24
 **/
gboolean
camel_folder_summary_header_load_from_db (CamelFolderSummary *summary,
                                          CamelStore *store,
                                          const gchar *folder_name,
                                          GError **error)
{
	CamelDB *cdb;
	CamelFIRecord *record;
	gboolean ret = FALSE;

	d (printf ("\ncamel_folder_summary_header_load_from_db called \n"));

	if (is_in_memory_summary (summary))
		return TRUE;

	camel_folder_summary_lock (summary);
	camel_folder_summary_save_to_db (summary, NULL);

	cdb = store->cdb_r;

	record = g_new0 (CamelFIRecord, 1);
	camel_db_read_folder_info_record (cdb, folder_name, record, error);

	ret = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->summary_header_from_db (summary, record);

	camel_folder_summary_unlock (summary);

	g_free (record->folder_name);
	g_free (record->bdata);
	g_free (record);

	return ret;
}

static gboolean
summary_assign_uid (CamelFolderSummary *summary,
                    CamelMessageInfo *info)
{
	const gchar *uid;
	CamelMessageInfo *mi;

	uid = camel_message_info_uid (info);

	if (uid == NULL || uid[0] == 0) {
		camel_pstring_free (info->uid);
		uid = info->uid = (gchar *) camel_pstring_add (camel_folder_summary_next_uid_string (summary), TRUE);
	}

	camel_folder_summary_lock (summary);

	while ((mi = g_hash_table_lookup (summary->priv->loaded_infos, uid))) {
		camel_folder_summary_unlock (summary);

		if (mi == info)
			return FALSE;

		d (printf ("Trying to insert message with clashing uid (%s).  new uid re-assigned", camel_message_info_uid (info)));

		camel_pstring_free (info->uid);
		uid = info->uid = camel_pstring_add (camel_folder_summary_next_uid_string (summary), TRUE);
		camel_message_info_set_flags (info, CAMEL_MESSAGE_FOLDER_FLAGGED, CAMEL_MESSAGE_FOLDER_FLAGGED);

		camel_folder_summary_lock (summary);
	}

	camel_folder_summary_unlock (summary);

	return TRUE;
}

/**
 * camel_folder_summary_add:
 * @summary: a #CamelFolderSummary object
 * @info: a #CamelMessageInfo
 *
 * Adds a new @info record to the summary.  If @info->uid is %NULL,
 * then a new uid is automatically re-assigned by calling
 * camel_folder_summary_next_uid_string().
 *
 * The @info record should have been generated by calling one of the
 * info_new_*() functions, as it will be free'd based on the summary
 * class.  And MUST NOT be allocated directly using malloc.
 **/
void
camel_folder_summary_add (CamelFolderSummary *summary,
                          CamelMessageInfo *info)
{
	CamelMessageInfoBase *base_info;

	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	if (info == NULL)
		return;

	camel_folder_summary_lock (summary);
	if (!summary_assign_uid (summary, info)) {
		camel_folder_summary_unlock (summary);
		return;
	}

	base_info = (CamelMessageInfoBase *) info;
	folder_summary_update_counts_by_flags (summary, camel_message_info_flags (info), UPDATE_COUNTS_ADD);
	base_info->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
	base_info->dirty = TRUE;

	g_hash_table_insert (
		summary->priv->uids,
		(gpointer) camel_pstring_strdup (camel_message_info_uid (info)),
		GUINT_TO_POINTER (camel_message_info_flags (info)));

	/* Summary always holds a ref for the loaded infos */
	g_hash_table_insert (summary->priv->loaded_infos, (gpointer) camel_message_info_uid (info), info);

	camel_folder_summary_touch (summary);

	camel_folder_summary_unlock (summary);
}

/**
 * camel_folder_summary_insert:
 *
 * Since: 2.24
 **/
void
camel_folder_summary_insert (CamelFolderSummary *summary,
                             CamelMessageInfo *info,
                             gboolean load)
{
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	if (info == NULL)
		return;

	camel_folder_summary_lock (summary);

	if (!load) {
		CamelMessageInfoBase *base_info = (CamelMessageInfoBase *) info;

		folder_summary_update_counts_by_flags (summary, camel_message_info_flags (info), UPDATE_COUNTS_ADD);
		base_info->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		base_info->dirty = TRUE;

		g_hash_table_insert (
			summary->priv->uids,
			(gpointer) camel_pstring_strdup (camel_message_info_uid (info)),
			GUINT_TO_POINTER (camel_message_info_flags (info)));

		camel_folder_summary_touch (summary);
	}

	/* Summary always holds a ref for the loaded infos */
	g_hash_table_insert (summary->priv->loaded_infos, (gchar *) camel_message_info_uid (info), info);

	camel_folder_summary_unlock (summary);
}

/**
 * camel_folder_summary_info_new_from_header:
 * @summary: a #CamelFolderSummary object
 * @headers: rfc822 headers
 *
 * Create a new info record from a header.
 *
 * Returns: the newly allocated record which must be unreferenced with
 *          camel_message_info_unref()
 **/
CamelMessageInfo *
camel_folder_summary_info_new_from_header (CamelFolderSummary *summary,
                                           struct _camel_header_raw *h)
{
	CamelFolderSummaryClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);

	class = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary);
	g_return_val_if_fail (class->message_info_new_from_header != NULL, NULL);

	return class->message_info_new_from_header (summary, h);
}

/**
 * camel_folder_summary_info_new_from_parser:
 * @summary: a #CamelFolderSummary object
 * @parser: a #CamelMimeParser object
 *
 * Create a new info record from a parser.  If the parser cannot
 * determine a uid, then none will be assigned.
 *
 * If indexing is enabled, and the parser cannot determine a new uid, then
 * one is automatically assigned.
 *
 * If indexing is enabled, then the content will be indexed based
 * on this new uid.  In this case, the message info MUST be
 * added using :add().
 *
 * Once complete, the parser will be positioned at the end of
 * the message.
 *
 * Returns: the newly allocated record which must be unreferenced with
 *          camel_message_info_unref()
 **/
CamelMessageInfo *
camel_folder_summary_info_new_from_parser (CamelFolderSummary *summary,
                                           CamelMimeParser *mp)
{
	CamelMessageInfo *info = NULL;
	gchar *buffer;
	gsize len;
	CamelFolderSummaryPrivate *p = summary->priv;
	goffset start;
	CamelIndexName *name = NULL;

	/* should this check the parser is in the right state, or assume it is?? */

	start = camel_mime_parser_tell (mp);
	if (camel_mime_parser_step (mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_EOF) {
		info = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_new_from_parser (summary, mp);

		camel_mime_parser_unstep (mp);

		/* assign a unique uid, this is slightly 'wrong' as we do not really
		 * know if we are going to store this in the summary, but no matter */
		if (p->index)
			summary_assign_uid (summary, info);

		g_rec_mutex_lock (&summary->priv->filter_lock);

		if (p->index) {
			if (p->filter_index == NULL)
				p->filter_index = camel_mime_filter_index_new (p->index);
			camel_index_delete_name (p->index, camel_message_info_uid (info));
			name = camel_index_add_name (p->index, camel_message_info_uid (info));
			camel_mime_filter_index_set_name (CAMEL_MIME_FILTER_INDEX (p->filter_index), name);
		}

		/* always scan the content info, even if we dont save it */
		((CamelMessageInfoBase *) info)->content = summary_build_content_info (summary, info, mp);

		if (name && p->index) {
			camel_index_write_name (p->index, name);
			g_object_unref (name);
			camel_mime_filter_index_set_name (
				CAMEL_MIME_FILTER_INDEX (p->filter_index), NULL);
		}

		g_rec_mutex_unlock (&summary->priv->filter_lock);

		((CamelMessageInfoBase *) info)->size = camel_mime_parser_tell (mp) - start;
	}
	return info;
}

/**
 * camel_folder_summary_info_new_from_message:
 * @summary: a #CamelFolderSummary object
 * @message: a #CamelMimeMessage object
 * @bodystructure: a bodystructure or NULL
 *
 * Create a summary item from a message.
 *
 * Returns: the newly allocated record which must be unreferenced with
 *          camel_message_info_unref()
 **/
CamelMessageInfo *
camel_folder_summary_info_new_from_message (CamelFolderSummary *summary,
                                            CamelMimeMessage *msg,
                                            const gchar *bodystructure)
{
	CamelMessageInfo *info;
	CamelFolderSummaryPrivate *p = summary->priv;
	CamelIndexName *name = NULL;

	info = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_new_from_message (summary, msg, bodystructure);

	/* assign a unique uid, this is slightly 'wrong' as we do not really
	 * know if we are going to store this in the summary, but we need it set for indexing */
	if (p->index)
		summary_assign_uid (summary, info);

	g_rec_mutex_lock (&summary->priv->filter_lock);

	if (p->index) {
		if (p->filter_index == NULL)
			p->filter_index = camel_mime_filter_index_new (p->index);
		camel_index_delete_name (p->index, camel_message_info_uid (info));
		name = camel_index_add_name (p->index, camel_message_info_uid (info));
		camel_mime_filter_index_set_name (
			CAMEL_MIME_FILTER_INDEX (p->filter_index), name);

		if (p->filter_stream == NULL) {
			CamelStream *null = camel_stream_null_new ();

			p->filter_stream = camel_stream_filter_new (null);
			g_object_unref (null);
		}
	}

	((CamelMessageInfoBase *) info)->content = summary_build_content_info_message (summary, info, (CamelMimePart *) msg);

	if (name) {
		camel_index_write_name (p->index, name);
		g_object_unref (name);
		camel_mime_filter_index_set_name (
			CAMEL_MIME_FILTER_INDEX (p->filter_index), NULL);
	}

	g_rec_mutex_unlock (&summary->priv->filter_lock);

	return info;
}

/**
 * camel_folder_summary_content_info_free:
 * @summary: a #CamelFolderSummary object
 * @ci: a #CamelMessageContentInfo
 *
 * Free the content info @ci, and all associated memory.
 **/
void
camel_folder_summary_content_info_free (CamelFolderSummary *summary,
                                        CamelMessageContentInfo *ci)
{
	CamelMessageContentInfo *pw, *pn;

	pw = ci->childs;
	CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->content_info_free (summary, ci);
	while (pw) {
		pn = pw->next;
		camel_folder_summary_content_info_free (summary, pw);
		pw = pn;
	}
}

/**
 * camel_folder_summary_touch:
 * @summary: a #CamelFolderSummary object
 *
 * Mark the summary as changed, so that a save will force it to be
 * written back to disk.
 **/
void
camel_folder_summary_touch (CamelFolderSummary *summary)
{
	camel_folder_summary_lock (summary);
	summary->flags |= CAMEL_FOLDER_SUMMARY_DIRTY;
	camel_folder_summary_unlock (summary);
}

/**
 * camel_folder_summary_clear:
 * @summary: a #CamelFolderSummary object
 *
 * Empty the summary contents.
 **/
gboolean
camel_folder_summary_clear (CamelFolderSummary *summary,
                            GError **error)
{
	GObject *summary_object;
	CamelStore *parent_store;
	CamelDB *cdb;
	const gchar *folder_name;
	gboolean res;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);

	camel_folder_summary_lock (summary);
	if (camel_folder_summary_count (summary) == 0) {
		camel_folder_summary_unlock (summary);
		return TRUE;
	}

	g_hash_table_remove_all (summary->priv->uids);
	remove_all_loaded (summary);
	g_hash_table_remove_all (summary->priv->loaded_infos);

	summary->priv->saved_count = 0;
	summary->priv->unread_count = 0;
	summary->priv->deleted_count = 0;
	summary->priv->junk_count = 0;
	summary->priv->junk_not_deleted_count = 0;
	summary->priv->visible_count = 0;

	camel_folder_summary_touch (summary);

	folder_name = camel_folder_get_full_name (summary->priv->folder);
	parent_store = camel_folder_get_parent_store (summary->priv->folder);
	cdb = parent_store->cdb_w;

	if (!is_in_memory_summary (summary))
		res = camel_db_clear_folder_summary (cdb, folder_name, error) == 0;
	else
		res = TRUE;

	summary_object = G_OBJECT (summary);
	g_object_freeze_notify (summary_object);
	g_object_notify (summary_object, "saved-count");
	g_object_notify (summary_object, "unread-count");
	g_object_notify (summary_object, "deleted-count");
	g_object_notify (summary_object, "junk-count");
	g_object_notify (summary_object, "junk-not-deleted-count");
	g_object_notify (summary_object, "visible-count");
	g_object_thaw_notify (summary_object);

	camel_folder_summary_unlock (summary);

	return res;
}

/**
 * camel_folder_summary_remove:
 * @summary: a #CamelFolderSummary object
 * @info: a #CamelMessageInfo
 *
 * Remove a specific @info record from the summary.
 *
 * Returns: Whether the @info was found and removed from the @summary.
 **/
gboolean
camel_folder_summary_remove (CamelFolderSummary *summary,
                             CamelMessageInfo *info)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);
	g_return_val_if_fail (info != NULL, FALSE);

	if (camel_folder_summary_remove_uid (summary, camel_message_info_uid (info))) {
		camel_message_info_unref (info);
		return TRUE;
	}

	return FALSE;
}

/**
 * camel_folder_summary_remove_uid:
 * @summary: a #CamelFolderSummary object
 * @uid: a uid
 *
 * Remove a specific info record from the summary, by @uid.
 *
 * Returns: Whether the @uid was found and removed from the @summary.
 **/
gboolean
camel_folder_summary_remove_uid (CamelFolderSummary *summary,
                                 const gchar *uid)
{
	gpointer ptr_uid = NULL, ptr_flags = NULL;
	CamelStore *parent_store;
	const gchar *full_name;
	const gchar *uid_copy;
	gboolean res = TRUE;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	camel_folder_summary_lock (summary);
	if (!g_hash_table_lookup_extended (summary->priv->uids, uid, &ptr_uid, &ptr_flags)) {
		camel_folder_summary_unlock (summary);
		return FALSE;
	}

	folder_summary_update_counts_by_flags (summary, GPOINTER_TO_UINT (ptr_flags), UPDATE_COUNTS_SUB);

	uid_copy = camel_pstring_strdup (uid);
	g_hash_table_remove (summary->priv->uids, uid_copy);
	g_hash_table_remove (summary->priv->loaded_infos, uid_copy);

	if (!is_in_memory_summary (summary)) {
		full_name = camel_folder_get_full_name (summary->priv->folder);
		parent_store = camel_folder_get_parent_store (summary->priv->folder);
		if (camel_db_delete_uid (parent_store->cdb_w, full_name, uid_copy, NULL) != 0)
			res = FALSE;
	}

	camel_pstring_free (uid_copy);

	camel_folder_summary_touch (summary);
	camel_folder_summary_unlock (summary);

	return res;
}

/**
 * camel_folder_summary_remove_uids:
 * @summary: a #CamelFolderSummary object
 * @uids: a GList of uids
 *
 * Remove a specific info record from the summary, by @uid.
 *
 * Returns: Whether the @uid was found and removed from the @summary.
 *
 * Since: 3.6
 **/
gboolean
camel_folder_summary_remove_uids (CamelFolderSummary *summary,
                                  GList *uids)
{
	CamelStore *parent_store;
	const gchar *full_name;
	GList *l;
	gboolean res = TRUE;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	g_object_freeze_notify (G_OBJECT (summary));
	camel_folder_summary_lock (summary);

	for (l = g_list_first (uids); l; l = g_list_next (l)) {
		gpointer ptr_uid = NULL, ptr_flags = NULL;
		if (g_hash_table_lookup_extended (summary->priv->uids, l->data, &ptr_uid, &ptr_flags)) {
			const gchar *uid_copy = camel_pstring_strdup (l->data);
			CamelMessageInfo *mi;

			folder_summary_update_counts_by_flags (summary, GPOINTER_TO_UINT (ptr_flags), UPDATE_COUNTS_SUB);
			g_hash_table_remove (summary->priv->uids, uid_copy);

			mi = g_hash_table_lookup (summary->priv->loaded_infos, uid_copy);
			g_hash_table_remove (summary->priv->loaded_infos, uid_copy);

			if (mi)
				camel_message_info_unref (mi);
			camel_pstring_free (uid_copy);
		}
	}

	if (!is_in_memory_summary (summary)) {
		full_name = camel_folder_get_full_name (summary->priv->folder);
		parent_store = camel_folder_get_parent_store (summary->priv->folder);
		if (camel_db_delete_uids (parent_store->cdb_w, full_name, uids, NULL) != 0)
			res = FALSE;
	}

	camel_folder_summary_touch (summary);
	camel_folder_summary_unlock (summary);
	g_object_thaw_notify (G_OBJECT (summary));

	return res;
}

static struct _node *
my_list_append (struct _node **list,
                struct _node *n)
{
	struct _node *ln = *list;
	n->next = NULL;

	if (!ln) {
		*list = n;
		return n;
	}

	while (ln->next)
		ln = ln->next;
	ln->next = n;
	return n;
}

static gint
my_list_size (struct _node **list)
{
	gint len = 0;
	struct _node *ln = (struct _node *) list;
	while (ln->next) {
		ln = ln->next;
		len++;
	}
	return len;
}

/* are these even useful for anything??? */
static CamelMessageInfo *
message_info_new_from_parser (CamelFolderSummary *summary,
                              CamelMimeParser *mp)
{
	CamelMessageInfo *mi = NULL;
	gint state;

	state = camel_mime_parser_state (mp);
	switch (state) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		mi = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->message_info_new_from_header (summary, camel_mime_parser_headers_raw (mp));
		break;
	default:
		g_error ("Invalid parser state");
	}

	return mi;
}

static CamelMessageContentInfo *
content_info_new_from_parser (CamelFolderSummary *summary,
                              CamelMimeParser *mp)
{
	CamelMessageContentInfo *ci = NULL;

	switch (camel_mime_parser_state (mp)) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		ci = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->content_info_new_from_header (summary, camel_mime_parser_headers_raw (mp));
		if (ci) {
			if (ci->type)
				camel_content_type_unref (ci->type);
			ci->type = camel_mime_parser_content_type (mp);
			camel_content_type_ref (ci->type);
		}
		break;
	default:
		g_error ("Invalid parser state");
	}

	return ci;
}

static CamelMessageInfo *
message_info_new_from_message (CamelFolderSummary *summary,
                               CamelMimeMessage *msg,
                               const gchar *bodystructure)
{
	CamelMessageInfo *mi;

	mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS (summary)))->message_info_new_from_header (summary, ((CamelMimePart *) msg)->headers);
	((CamelMessageInfoBase *) mi)->bodystructure = g_strdup (bodystructure);

	return mi;
}

static CamelMessageContentInfo *
content_info_new_from_message (CamelFolderSummary *summary,
                               CamelMimePart *mp)
{
	CamelMessageContentInfo *ci;

	ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS (summary)))->content_info_new_from_header (summary, mp->headers);

	return ci;
}

static gchar *
summary_format_address (struct _camel_header_raw *h,
                        const gchar *name,
                        const gchar *charset)
{
	struct _camel_header_address *addr;
	gchar *text, *str;

	if (!(text = (gchar *) camel_header_raw_find (&h, name, NULL)))
		return NULL;

	while (isspace ((unsigned) *text))
		text++;

	text = camel_header_unfold (text);

	if ((addr = camel_header_address_decode (text, charset))) {
		str = camel_header_address_list_format (addr);
		camel_header_address_list_clear (&addr);
		g_free (text);
	} else {
		str = text;
	}

	return str;
}

static gchar *
summary_format_string (struct _camel_header_raw *h,
                       const gchar *name,
                       const gchar *charset)
{
	gchar *text, *str;

	if (!(text = (gchar *) camel_header_raw_find (&h, name, NULL)))
		return NULL;

	while (isspace ((unsigned) *text))
		text++;

	text = camel_header_unfold (text);
	str = camel_header_decode_string (text, charset);
	g_free (text);

	return str;
}

/**
 * camel_folder_summary_content_info_new:
 * @summary: a #CamelFolderSummary object
 *
 * Allocate a new #CamelMessageContentInfo, suitable for adding
 * to this summary.
 *
 * Returns: a newly allocated #CamelMessageContentInfo
 **/
CamelMessageContentInfo *
camel_folder_summary_content_info_new (CamelFolderSummary *summary)
{
	CamelFolderSummaryClass *class;

	class = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary);
	g_return_val_if_fail (class->content_info_size > 0, NULL);

	return g_slice_alloc0 (class->content_info_size);
}

static CamelMessageInfo *
message_info_new_from_header (CamelFolderSummary *summary,
                              struct _camel_header_raw *h)
{
	const gchar *received, *date, *content, *charset = NULL;
	struct _camel_header_references *refs, *irt, *scan;
	gchar *subject, *from, *to, *cc, *mlist;
	CamelContentType *ct = NULL;
	CamelMessageInfoBase *mi;
	guint8 *digest;
	gsize length;
	gchar *msgid;
	gint count;

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	mi = (CamelMessageInfoBase *) camel_message_info_new (summary);

	if ((content = camel_header_raw_find (&h, "Content-Type", NULL))
	     && (ct = camel_content_type_decode (content))
	     && (charset = camel_content_type_param (ct, "charset"))
	     && (g_ascii_strcasecmp (charset, "us-ascii") == 0))
		charset = NULL;

	charset = charset ? camel_iconv_charset_name (charset) : NULL;

	subject = summary_format_string (h, "subject", charset);
	from = summary_format_address (h, "from", charset);
	to = summary_format_address (h, "to", charset);
	cc = summary_format_address (h, "cc", charset);
	mlist = camel_header_raw_check_mailing_list (&h);

	if (ct)
		camel_content_type_unref (ct);

	mi->subject = camel_pstring_add (subject, TRUE);
	mi->from = camel_pstring_add (from, TRUE);
	mi->to = camel_pstring_add (to, TRUE);
	mi->cc = camel_pstring_add (cc, TRUE);
	mi->mlist = camel_pstring_add (mlist, TRUE);

	mi->user_flags = NULL;
	mi->user_tags = NULL;

	if ((date = camel_header_raw_find (&h, "date", NULL)))
		mi->date_sent = camel_header_decode_date (date, NULL);
	else
		mi->date_sent = 0;

	received = camel_header_raw_find (&h, "received", NULL);
	if (received)
		received = strrchr (received, ';');
	if (received)
		mi->date_received = camel_header_decode_date (received + 1, NULL);
	else
		mi->date_received = 0;

	msgid = camel_header_msgid_decode (camel_header_raw_find (&h, "message-id", NULL));
	if (msgid) {
		GChecksum *checksum;

		checksum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (checksum, (guchar *) msgid, -1);
		g_checksum_get_digest (checksum, digest, &length);
		g_checksum_free (checksum);

		memcpy (mi->message_id.id.hash, digest, sizeof (mi->message_id.id.hash));
		g_free (msgid);
	}

	/* decode our references and in-reply-to headers */
	refs = camel_header_references_decode (camel_header_raw_find (&h, "references", NULL));
	irt = camel_header_references_inreplyto_decode (camel_header_raw_find (&h, "in-reply-to", NULL));
	if (refs || irt) {
		if (irt) {
			/* The References field is populated from the "References" and/or "In-Reply-To"
			 * headers. If both headers exist, take the first thing in the In-Reply-To header
			 * that looks like a Message-ID, and append it to the References header. */

			if (refs)
				irt->next = refs;

			refs = irt;
		}

		count = camel_header_references_list_size (&refs);
		mi->references = g_malloc (sizeof (*mi->references) + ((count - 1) * sizeof (mi->references->references[0])));
		count = 0;
		scan = refs;
		while (scan) {
			GChecksum *checksum;

			checksum = g_checksum_new (G_CHECKSUM_MD5);
			g_checksum_update (checksum, (guchar *) scan->id, -1);
			g_checksum_get_digest (checksum, digest, &length);
			g_checksum_free (checksum);

			memcpy (mi->references->references[count].id.hash, digest, sizeof (mi->message_id.id.hash));
			count++;
			scan = scan->next;
		}
		mi->references->size = count;
		camel_header_references_list_clear (&refs);
	}

	return (CamelMessageInfo *) mi;
}

static void
message_info_free (CamelFolderSummary *summary,
                   CamelMessageInfo *info)
{
	CamelFolderSummaryClass *class;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *) info;

	if (mi->uid) {
		if (summary) {
			camel_folder_summary_lock (summary);
			if (g_hash_table_lookup (summary->priv->loaded_infos, mi->uid) == mi) {
				g_hash_table_remove (summary->priv->loaded_infos, mi->uid);
			}
			camel_folder_summary_unlock (summary);
		}
		camel_pstring_free (mi->uid);
	}
	camel_pstring_free (mi->subject);
	camel_pstring_free (mi->from);
	camel_pstring_free (mi->to);
	camel_pstring_free (mi->cc);
	camel_pstring_free (mi->mlist);
	g_free (mi->bodystructure);
	g_free (mi->references);
	g_free (mi->preview);
	camel_flag_list_free (&mi->user_flags);
	camel_tag_list_free (&mi->user_tags);
	if (mi->headers)
		camel_header_param_list_free (mi->headers);

	if (summary) {
		class = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary);
		g_slice_free1 (class->message_info_size, mi);
	} else
		g_slice_free (CamelMessageInfoBase, mi);
}

static CamelMessageContentInfo *
content_info_new_from_header (CamelFolderSummary *summary,
                              struct _camel_header_raw *h)
{
	CamelMessageContentInfo *ci;
	const gchar *charset;

	ci = camel_folder_summary_content_info_new (summary);

	charset = camel_iconv_locale_charset ();
	ci->id = camel_header_msgid_decode (camel_header_raw_find (&h, "content-id", NULL));
	ci->description = camel_header_decode_string (camel_header_raw_find (&h, "content-description", NULL), charset);
	ci->encoding = camel_content_transfer_encoding_decode (camel_header_raw_find (&h, "content-transfer-encoding", NULL));
	ci->type = camel_content_type_decode (camel_header_raw_find (&h, "content-type", NULL));

	return ci;
}

static void
content_info_free (CamelFolderSummary *summary,
                   CamelMessageContentInfo *ci)
{
	CamelFolderSummaryClass *class;

	class = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary);

	camel_content_type_unref (ci->type);
	g_free (ci->id);
	g_free (ci->description);
	g_free (ci->encoding);
	g_slice_free1 (class->content_info_size, ci);
}

static gchar *
next_uid_string (CamelFolderSummary *summary)
{
	return g_strdup_printf ("%u", camel_folder_summary_next_uid (summary));
}

/*
  OK
  Now this is where all the "smarts" happen, where the content info is built,
  and any indexing and what not is performed
*/

/* must have filter_lock before calling this function */
static CamelMessageContentInfo *
summary_build_content_info (CamelFolderSummary *summary,
                            CamelMessageInfo *msginfo,
                            CamelMimeParser *mp)
{
	gint state;
	gsize len;
	gchar *buffer;
	CamelMessageContentInfo *info = NULL;
	CamelContentType *ct;
	gint enc_id = -1, chr_id = -1, html_id = -1, idx_id = -1;
	CamelFolderSummaryPrivate *p = summary->priv;
	CamelMimeFilter *mfc;
	CamelMessageContentInfo *part;
	const gchar *calendar_header;

	d (printf ("building content info\n"));

	/* start of this part */
	state = camel_mime_parser_step (mp, &buffer, &len);

	if (summary->priv->build_content)
		info = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->content_info_new_from_parser (summary, mp);

	switch (state) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
		/* check content type for indexing, then read body */
		ct = camel_mime_parser_content_type (mp);
		/* update attachments flag as we go */
		if (camel_content_type_is (ct, "application", "pgp-signature")
#ifdef ENABLE_SMIME
		    || camel_content_type_is (ct, "application", "x-pkcs7-signature")
		    || camel_content_type_is (ct, "application", "pkcs7-signature")
#endif
			)
			camel_message_info_set_flags (msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);

		calendar_header = camel_mime_parser_header (mp, "Content-class", NULL);
		if (calendar_header && g_ascii_strcasecmp (calendar_header, "urn:content-classes:calendarmessage") != 0)
			calendar_header = NULL;

		if (!calendar_header)
			calendar_header = camel_mime_parser_header (mp, "X-Calendar-Attachment", NULL);

		if (calendar_header || camel_content_type_is (ct, "text", "calendar"))
			camel_message_info_set_user_flag (msginfo, "$has_cal", TRUE);

		if (p->index && camel_content_type_is (ct, "text", "*")) {
			gchar *encoding;
			const gchar *charset;

			d (printf ("generating index:\n"));

			encoding = camel_content_transfer_encoding_decode (camel_mime_parser_header (mp, "content-transfer-encoding", NULL));
			if (encoding) {
				if (!g_ascii_strcasecmp (encoding, "base64")) {
					d (printf (" decoding base64\n"));
					if (p->filter_64 == NULL)
						p->filter_64 = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_BASE64_DEC);
					else
						camel_mime_filter_reset (p->filter_64);
					enc_id = camel_mime_parser_filter_add (mp, p->filter_64);
				} else if (!g_ascii_strcasecmp (encoding, "quoted-printable")) {
					d (printf (" decoding quoted-printable\n"));
					if (p->filter_qp == NULL)
						p->filter_qp = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_QP_DEC);
					else
						camel_mime_filter_reset (p->filter_qp);
					enc_id = camel_mime_parser_filter_add (mp, p->filter_qp);
				} else if (!g_ascii_strcasecmp (encoding, "x-uuencode")) {
					d (printf (" decoding x-uuencode\n"));
					if (p->filter_uu == NULL)
						p->filter_uu = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_UU_DEC);
					else
						camel_mime_filter_reset (p->filter_uu);
					enc_id = camel_mime_parser_filter_add (mp, p->filter_uu);
				} else {
					d (printf (" ignoring encoding %s\n", encoding));
				}
				g_free (encoding);
			}

			charset = camel_content_type_param (ct, "charset");
			if (charset != NULL
			    && !(g_ascii_strcasecmp (charset, "us-ascii") == 0
				 || g_ascii_strcasecmp (charset, "utf-8") == 0)) {
				d (printf (" Adding conversion filter from %s to UTF-8\n", charset));
				mfc = g_hash_table_lookup (p->filter_charset, charset);
				if (mfc == NULL) {
					mfc = camel_mime_filter_charset_new (charset, "UTF-8");
					if (mfc)
						g_hash_table_insert (p->filter_charset, g_strdup (charset), mfc);
				} else {
					camel_mime_filter_reset ((CamelMimeFilter *) mfc);
				}
				if (mfc) {
					chr_id = camel_mime_parser_filter_add (mp, mfc);
				} else {
					w (g_warning ("Cannot convert '%s' to 'UTF-8', message index may be corrupt", charset));
				}
			}

			/* we do charset conversions before this filter, which isn't strictly correct,
			 * but works in most cases */
			if (camel_content_type_is (ct, "text", "html")) {
				if (p->filter_html == NULL)
					p->filter_html = camel_mime_filter_html_new ();
				else
					camel_mime_filter_reset ((CamelMimeFilter *) p->filter_html);
				html_id = camel_mime_parser_filter_add (mp, (CamelMimeFilter *) p->filter_html);
			}

			/* and this filter actually does the indexing */
			idx_id = camel_mime_parser_filter_add (mp, p->filter_index);
		}
		/* and scan/index everything */
		while (camel_mime_parser_step (mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_BODY_END)
			;
		/* and remove the filters */
		camel_mime_parser_filter_remove (mp, enc_id);
		camel_mime_parser_filter_remove (mp, chr_id);
		camel_mime_parser_filter_remove (mp, html_id);
		camel_mime_parser_filter_remove (mp, idx_id);
		break;
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		d (printf ("Summarising multipart\n"));
		/* update attachments flag as we go */
		ct = camel_mime_parser_content_type (mp);
		if (camel_content_type_is (ct, "multipart", "mixed"))
			camel_message_info_set_flags (msginfo, CAMEL_MESSAGE_ATTACHMENTS, CAMEL_MESSAGE_ATTACHMENTS);
		if (camel_content_type_is (ct, "multipart", "signed")
		    || camel_content_type_is (ct, "multipart", "encrypted"))
			camel_message_info_set_flags (msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);

		while (camel_mime_parser_step (mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_MULTIPART_END) {
			camel_mime_parser_unstep (mp);
			part = summary_build_content_info (summary, msginfo, mp);
			if (part) {
				part->parent = info;
				my_list_append ((struct _node **) &info->childs, (struct _node *) part);
			}
		}
		break;
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
		d (printf ("Summarising message\n"));
		/* update attachments flag as we go */
		camel_message_info_set_flags (msginfo, CAMEL_MESSAGE_ATTACHMENTS, CAMEL_MESSAGE_ATTACHMENTS);

		part = summary_build_content_info (summary, msginfo, mp);
		if (part) {
			part->parent = info;
			my_list_append ((struct _node **) &info->childs, (struct _node *) part);
		}
		state = camel_mime_parser_step (mp, &buffer, &len);
		if (state != CAMEL_MIME_PARSER_STATE_MESSAGE_END) {
			g_error ("Bad parser state: Expecing MESSAGE_END or MESSAGE_EOF, got: %d", state);
			camel_mime_parser_unstep (mp);
		}
		break;
	}

	d (printf ("finished building content info\n"));

	return info;
}

/* build the content-info, from a message */
/* this needs the filter lock since it uses filters to perform indexing */
static CamelMessageContentInfo *
summary_build_content_info_message (CamelFolderSummary *summary,
                                    CamelMessageInfo *msginfo,
                                    CamelMimePart *object)
{
	CamelDataWrapper *containee;
	gint parts, i;
	CamelFolderSummaryPrivate *p = summary->priv;
	CamelMessageContentInfo *info = NULL, *child;
	CamelContentType *ct;
	const struct _camel_header_raw *header;

	if (summary->priv->build_content)
		info = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->content_info_new_from_message (summary, object);

	containee = camel_medium_get_content (CAMEL_MEDIUM (object));

	if (containee == NULL)
		return info;

	/* TODO: I find it odd that get_part and get_content do not
	 * add a reference, probably need fixing for multithreading */

	/* check for attachments */
	ct = ((CamelDataWrapper *) containee)->mime_type;
	if (camel_content_type_is (ct, "multipart", "*")) {
		if (camel_content_type_is (ct, "multipart", "mixed"))
			camel_message_info_set_flags (msginfo, CAMEL_MESSAGE_ATTACHMENTS, CAMEL_MESSAGE_ATTACHMENTS);
		if (camel_content_type_is (ct, "multipart", "signed")
		    || camel_content_type_is (ct, "multipart", "encrypted"))
			camel_message_info_set_flags (msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);
	} else if (camel_content_type_is (ct, "application", "pgp-signature")
#ifdef ENABLE_SMIME
		    || camel_content_type_is (ct, "application", "x-pkcs7-signature")
		    || camel_content_type_is (ct, "application", "pkcs7-signature")
#endif
		) {
		camel_message_info_set_flags (msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);
	}

	for (header = object->headers; header; header = header->next) {
		const gchar *value = header->value;

		/* skip preceding spaces in the value */
		while (value && *value && isspace (*value))
			value++;

		if (header->name && value && (
		    (g_ascii_strcasecmp (header->name, "Content-class") == 0 && g_ascii_strcasecmp (value, "urn:content-classes:calendarmessage") == 0) ||
		    (g_ascii_strcasecmp (header->name, "X-Calendar-Attachment") == 0)))
			break;
	}

	if (header || camel_content_type_is (ct, "text", "calendar"))
		camel_message_info_set_user_flag (msginfo, "$has_cal", TRUE);

	/* using the object types is more accurate than using the mime/types */
	if (CAMEL_IS_MULTIPART (containee)) {
		parts = camel_multipart_get_number (CAMEL_MULTIPART (containee));

		for (i = 0; i < parts; i++) {
			CamelMimePart *part = camel_multipart_get_part (CAMEL_MULTIPART (containee), i);
			g_assert (part);
			child = summary_build_content_info_message (summary, msginfo, part);
			if (child) {
				child->parent = info;
				my_list_append ((struct _node **) &info->childs, (struct _node *) child);
			}
		}
	} else if (CAMEL_IS_MIME_MESSAGE (containee)) {
		/* for messages we only look at its contents */
		child = summary_build_content_info_message (summary, msginfo, (CamelMimePart *) containee);
		if (child) {
			child->parent = info;
			my_list_append ((struct _node **) &info->childs, (struct _node *) child);
		}
	} else if (p->filter_stream
		   && camel_content_type_is (ct, "text", "*")) {
		gint html_id = -1, idx_id = -1;

		/* pre-attach html filter if required, otherwise just index filter */
		if (camel_content_type_is (ct, "text", "html")) {
			if (p->filter_html == NULL)
				p->filter_html = camel_mime_filter_html_new ();
			else
				camel_mime_filter_reset ((CamelMimeFilter *) p->filter_html);
			html_id = camel_stream_filter_add (
				CAMEL_STREAM_FILTER (p->filter_stream),
				(CamelMimeFilter *) p->filter_html);
		}
		idx_id = camel_stream_filter_add (
			CAMEL_STREAM_FILTER (p->filter_stream),
			p->filter_index);

		/* FIXME Pass a GCancellable and GError here. */
		camel_data_wrapper_decode_to_stream_sync (
			containee, p->filter_stream, NULL, NULL);
		camel_stream_flush (p->filter_stream, NULL, NULL);

		camel_stream_filter_remove (
			CAMEL_STREAM_FILTER (p->filter_stream), idx_id);
		camel_stream_filter_remove (
			CAMEL_STREAM_FILTER (p->filter_stream), html_id);
	}

	return info;
}

/**
 * camel_flag_get:
 * @list: the address of a #CamelFlag list
 * @name: name of the flag to get
 *
 * Find the state of the flag @name in @list.
 *
 * Returns: the state of the flag (%TRUE or %FALSE)
 **/
gboolean
camel_flag_get (CamelFlag **list,
                const gchar *name)
{
	CamelFlag *flag;
	flag = *list;
	while (flag) {
		if (!strcmp (flag->name, name))
			return TRUE;
		flag = flag->next;
	}
	return FALSE;
}

/**
 * camel_flag_set:
 * @list: the address of a #CamelFlag list
 * @name: name of the flag to set or change
 * @value: the value to set on the flag
 *
 * Set the state of a flag @name in the list @list to @value.
 *
 * Returns: %TRUE if the value of the flag has been changed or %FALSE
 * otherwise
 **/
gboolean
camel_flag_set (CamelFlag **list,
                const gchar *name,
                gboolean value)
{
	CamelFlag *flag, *tmp;
	gsize tmp_len = 0;

	if (!name)
		return TRUE;

	/* this 'trick' works because flag->next is the first element */
	flag = (CamelFlag *) list;
	while (flag->next) {
		tmp = flag->next;
		if (!strcmp (flag->next->name, name)) {
			if (!value) {
				flag->next = tmp->next;
				g_free (tmp);
			}
			return !value;
		}
		flag = tmp;
	}

	if (value) {
		tmp_len = sizeof (*tmp) + strlen (name);
		tmp = g_malloc (tmp_len);
		g_strlcpy (tmp->name, name, strlen (name) + 1);
		tmp->next = NULL;
		flag->next = tmp;
	}
	return value;
}

/**
 * camel_flag_list_size:
 * @list: the address of a #CamelFlag list
 *
 * Get the length of the flag list.
 *
 * Returns: the number of flags in the list
 **/
gint
camel_flag_list_size (CamelFlag **list)
{
	gint count = 0;
	CamelFlag *flag;

	flag = *list;
	while (flag) {
		count++;
		flag = flag->next;
	}
	return count;
}

/**
 * camel_flag_list_free:
 * @list: the address of a #CamelFlag list
 *
 * Free the memory associated with the flag list @list.
 **/
void
camel_flag_list_free (CamelFlag **list)
{
	CamelFlag *flag, *tmp;
	flag = *list;
	while (flag) {
		tmp = flag->next;
		g_free (flag);
		flag = tmp;
	}
	*list = NULL;
}

/**
 * camel_flag_list_copy:
 * @to: the address of the #CamelFlag list to copy to
 * @from: the address of the #CamelFlag list to copy from
 *
 * Copy a flag list.
 *
 * Returns: %TRUE if @to is changed or %FALSE otherwise
 **/
gboolean
camel_flag_list_copy (CamelFlag **to,
                      CamelFlag **from)
{
	CamelFlag *flag, *tmp;
	gboolean changed = FALSE;

	if (*to == NULL && from == NULL)
		return FALSE;

	/* Remove any now-missing flags */
	flag = (CamelFlag *) to;
	while (flag->next) {
		tmp = flag->next;
		if (!camel_flag_get (from, tmp->name)) {
			if (*tmp->name)
				changed = TRUE;
			flag->next = tmp->next;
			g_free (tmp);
		} else {
			flag = tmp;
		}
	}

	/* Add any new non-empty flags */
	flag = *from;
	while (flag) {
		if (*flag->name)
			changed |= camel_flag_set (to, flag->name, TRUE);
		flag = flag->next;
	}

	return changed;
}

/**
 * camel_tag_get:
 * @list: the address of a #CamelTag list
 * @name: name of the tag to get
 *
 * Find the flag @name in @list and get the value.
 *
 * Returns: the value of the flag  or %NULL if unset
 **/
const gchar *
camel_tag_get (CamelTag **list,
               const gchar *name)
{
	CamelTag *tag;

	tag = *list;
	while (tag) {
		if (!strcmp (tag->name, name))
			return (const gchar *) tag->value;
		tag = tag->next;
	}
	return NULL;
}

/**
 * camel_tag_set:
 * @list: the address of a #CamelTag list
 * @name: name of the tag to set
 * @value: value to set on the tag
 *
 * Set the tag @name in the tag list @list to @value.
 *
 * Returns: %TRUE if the value on the tag changed or %FALSE otherwise
 **/
gboolean
camel_tag_set (CamelTag **list,
               const gchar *name,
               const gchar *value)
{
	CamelTag *tag, *tmp;

	/* this 'trick' works because tag->next is the first element */
	tag = (CamelTag *) list;
	while (tag->next) {
		tmp = tag->next;
		if (!strcmp (tmp->name, name)) {
			if (value == NULL) { /* clear it? */
				tag->next = tmp->next;
				g_free (tmp->value);
				g_free (tmp);
				return TRUE;
			} else if (strcmp (tmp->value, value)) { /* has it changed? */
				g_free (tmp->value);
				tmp->value = g_strdup (value);
				return TRUE;
			}
			return FALSE;
		}
		tag = tmp;
	}

	if (value) {
		tmp = g_malloc (sizeof (*tmp) + strlen (name));
		g_strlcpy (tmp->name, name, strlen (name) + 1);
		tmp->value = g_strdup (value);
		tmp->next = NULL;
		tag->next = tmp;
		return TRUE;
	}
	return FALSE;
}

/**
 * camel_tag_list_size:
 * @list: the address of a #CamelTag list
 *
 * Get the number of tags present in the tag list @list.
 *
 * Returns: the number of tags
 **/
gint
camel_tag_list_size (CamelTag **list)
{
	gint count = 0;
	CamelTag *tag;

	tag = *list;
	while (tag) {
		count++;
		tag = tag->next;
	}
	return count;
}

static void
rem_tag (gchar *key,
         gchar *value,
         CamelTag **to)
{
	camel_tag_set (to, key, NULL);
}

/**
 * camel_tag_list_copy:
 * @to: the address of the #CamelTag list to copy to
 * @from: the address of the #CamelTag list to copy from
 *
 * Copy a tag list.
 *
 * Returns: %TRUE if @to is changed or %FALSE otherwise
 **/
gboolean
camel_tag_list_copy (CamelTag **to,
                     CamelTag **from)
{
	gint changed = FALSE;
	CamelTag *tag;
	GHashTable *left;

	if (*to == NULL && from == NULL)
		return FALSE;

	left = g_hash_table_new (g_str_hash, g_str_equal);
	tag = *to;
	while (tag) {
		g_hash_table_insert (left, tag->name, tag);
		tag = tag->next;
	}

	tag = *from;
	while (tag) {
		changed |= camel_tag_set (to, tag->name, tag->value);
		g_hash_table_remove (left, tag->name);
		tag = tag->next;
	}

	if (g_hash_table_size (left) > 0) {
		g_hash_table_foreach (left, (GHFunc) rem_tag, to);
		changed = TRUE;
	}
	g_hash_table_destroy (left);

	return changed;
}

/**
 * camel_tag_list_free:
 * @list: the address of a #CamelTag list
 *
 * Free the tag list @list.
 **/
void
camel_tag_list_free (CamelTag **list)
{
	CamelTag *tag, *tmp;
	tag = *list;
	while (tag) {
		tmp = tag->next;
		g_free (tag->value);
		g_free (tag);
		tag = tmp;
	}
	*list = NULL;
}

static struct flag_names_t {
	const gchar *name;
	guint32 value;
} flag_names[] = {
	{ "answered", CAMEL_MESSAGE_ANSWERED },
	{ "deleted", CAMEL_MESSAGE_DELETED },
	{ "draft", CAMEL_MESSAGE_DRAFT },
	{ "flagged", CAMEL_MESSAGE_FLAGGED },
	{ "seen", CAMEL_MESSAGE_SEEN },
	{ "attachments", CAMEL_MESSAGE_ATTACHMENTS },
	{ "junk", CAMEL_MESSAGE_JUNK },
	{ "notjunk", CAMEL_MESSAGE_NOTJUNK },
	{ "secure", CAMEL_MESSAGE_SECURE },
	{ NULL, 0 }
};

/**
 * camel_system_flag:
 * @name: name of a system flag
 *
 * Returns: the integer value of the system flag string
 **/
CamelMessageFlags
camel_system_flag (const gchar *name)
{
	struct flag_names_t *flag;

	g_return_val_if_fail (name != NULL, 0);

	for (flag = flag_names; flag->name; flag++)
		if (!g_ascii_strcasecmp (name, flag->name))
			return flag->value;

	return 0;
}

/**
 * camel_system_flag_get:
 * @flags: bitwise system flags
 * @name: name of the flag to check for
 *
 * Find the state of the flag @name in @flags.
 *
 * Returns: %TRUE if the named flag is set or %FALSE otherwise
 **/
gboolean
camel_system_flag_get (CamelMessageFlags flags,
                       const gchar *name)
{
	g_return_val_if_fail (name != NULL, FALSE);

	return flags & camel_system_flag (name);
}

/**
 * camel_message_info_new:
 * @summary: a #CamelFolderSummary object or %NULL
 *
 * Create a new #CamelMessageInfo.
 *
 * Returns: a new #CamelMessageInfo
 **/
gpointer
camel_message_info_new (CamelFolderSummary *summary)
{
	CamelFolderSummaryClass *class;
	CamelMessageInfo *info;
	gsize message_info_size;

	if (summary != NULL) {
		class = CAMEL_FOLDER_SUMMARY_GET_CLASS (summary);
		g_return_val_if_fail (class->message_info_size > 0, NULL);
		message_info_size = class->message_info_size;
	} else {
		message_info_size = sizeof (CamelMessageInfoBase);
	}

	info = g_slice_alloc0 (message_info_size);
	info->refcount = 1;
	info->summary = summary;

	/* We assume that mi is always dirty unless freshly read or just saved*/
	((CamelMessageInfoBase *) info)->dirty = TRUE;

	return info;
}

/**
 * camel_message_info_ref:
 * @info: a #CamelMessageInfo
 *
 * Reference an info.
 **/
gpointer
camel_message_info_ref (gpointer o)
{
	CamelMessageInfo *mi = o;

	g_return_val_if_fail (mi != NULL, NULL);
	g_return_val_if_fail (mi->refcount > 0, NULL);

	g_atomic_int_inc (&mi->refcount);

	return o;
}

/**
 * camel_message_info_new_from_header:
 * @summary: a #CamelFolderSummary object or %NULL
 * @header: raw header
 *
 * Create a new #CamelMessageInfo pre-populated with info from
 * @header.
 *
 * Returns: a new #CamelMessageInfo
 **/
CamelMessageInfo *
camel_message_info_new_from_header (CamelFolderSummary *summary,
                                    struct _camel_header_raw *header)
{
	if (summary != NULL)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (summary)->
			message_info_new_from_header (summary, header);
	else
		return message_info_new_from_header (NULL, header);
}

/**
 * camel_message_info_unref:
 * @info: a #CamelMessageInfo
 *
 * Unref's and potentially frees a #CamelMessageInfo and its contents.
 **/
void
camel_message_info_unref (gpointer o)
{
	CamelMessageInfo *mi = o;

	g_return_if_fail (mi != NULL);
	g_return_if_fail (mi->refcount > 0);

	if (g_atomic_int_dec_and_test (&mi->refcount)) {
		if (mi->summary != NULL) {
			CamelFolderSummaryClass *class;

			/* FIXME This is kinda busted, should really
			 *       be handled by message_info_free(). */
			if (mi->summary->priv->build_content
			    && ((CamelMessageInfoBase *) mi)->content) {
				camel_folder_summary_content_info_free (
					mi->summary,
					((CamelMessageInfoBase *) mi)->content);
				((CamelMessageInfoBase *) mi)->content = NULL;
			}

			class = CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary);
			g_return_if_fail (class->message_info_free != NULL);

			class->message_info_free (mi->summary, mi);
		} else {
			message_info_free (NULL, mi);
		}
	}
}

/**
 * camel_message_info_clone:
 * @info: a #CamelMessageInfo
 *
 * Duplicate a #CamelMessageInfo.
 *
 * Returns: the duplicated #CamelMessageInfo
 **/
gpointer
camel_message_info_clone (gconstpointer o)
{
	const CamelMessageInfo *mi = o;

	if (mi->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->message_info_clone (mi->summary, mi);
	else
		return message_info_clone (NULL, mi);
}

/**
 * camel_message_info_ptr:
 * @info: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting pointer data.
 *
 * Returns: the pointer data
 **/
gconstpointer
camel_message_info_ptr (const CamelMessageInfo *info,
                        gint id)
{
	if (info->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (info->summary)->info_ptr (info, id);
	else
		return info_ptr (info, id);
}

/**
 * camel_message_info_uint32:
 * @info: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting 32bit gint data.
 *
 * Returns: the gint data
 **/
guint32
camel_message_info_uint32 (const CamelMessageInfo *info,
                           gint id)
{
	if (info->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (info->summary)->info_uint32 (info, id);
	else
		return info_uint32 (info, id);
}

/**
 * camel_message_info_time:
 * @info: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting time_t data.
 *
 * Returns: the time_t data
 **/
time_t
camel_message_info_time (const CamelMessageInfo *info,
                         gint id)
{
	if (info->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (info->summary)->info_time (info, id);
	else
		return info_time (info, id);
}

/**
 * camel_message_info_user_flag:
 * @info: a #CamelMessageInfo
 * @id: user flag to get
 *
 * Get the state of a user flag named @id.
 *
 * Returns: the state of the user flag
 **/
gboolean
camel_message_info_user_flag (const CamelMessageInfo *info,
                              const gchar *id)
{
	if (info->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (info->summary)->info_user_flag (info, id);
	else
		return info_user_flag (info, id);
}

/**
 * camel_message_info_user_tag:
 * @info: a #CamelMessageInfo
 * @id: user tag to get
 *
 * Get the value of a user tag named @id.
 *
 * Returns: the value of the user tag
 **/
const gchar *
camel_message_info_user_tag (const CamelMessageInfo *info,
                             const gchar *id)
{
	if (info->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (info->summary)->info_user_tag (info, id);
	else
		return info_user_tag (info, id);
}

/**
 * camel_message_info_set_flags:
 * @info: a #CamelMessageInfo
 * @flags: mask of flags to change
 * @set: state the flags should be changed to
 *
 * Change the state of the system flags on the #CamelMessageInfo
 *
 * Returns: %TRUE if any of the flags changed or %FALSE otherwise
 **/
gboolean
camel_message_info_set_flags (CamelMessageInfo *info,
                              CamelMessageFlags flags,
                              guint32 set)
{
	if (info->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (info->summary)->info_set_flags (info, flags, set);
	else
		return info_set_flags (info, flags, set);
}

/**
 * camel_message_info_set_user_flag:
 * @info: a #CamelMessageInfo
 * @id: name of the user flag to set
 * @state: state to set the flag to
 *
 * Set the state of a user flag on a #CamelMessageInfo.
 *
 * Returns: %TRUE if the state changed or %FALSE otherwise
 **/
gboolean
camel_message_info_set_user_flag (CamelMessageInfo *info,
                                  const gchar *id,
                                  gboolean state)
{
	if (info->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (info->summary)->info_set_user_flag (info, id, state);
	else
		return info_set_user_flag (info, id, state);
}

/**
 * camel_message_info_set_user_tag:
 * @info: a #CamelMessageInfo
 * @id: name of the user tag to set
 * @val: value to set
 *
 * Set the value of a user tag on a #CamelMessageInfo.
 *
 * Returns: %TRUE if the value changed or %FALSE otherwise
 **/
gboolean
camel_message_info_set_user_tag (CamelMessageInfo *info,
                                 const gchar *id,
                                 const gchar *val)
{
	if (info->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (info->summary)->info_set_user_tag (info, id, val);
	else
		return info_set_user_tag (info, id, val);
}

void
camel_content_info_dump (CamelMessageContentInfo *ci,
                         gint depth)
{
	gchar *p;

	p = alloca (depth * 4 + 1);
	memset (p, ' ', depth * 4);
	p[depth * 4] = 0;

	if (ci == NULL) {
		printf ("%s<empty>\n", p);
		return;
	}

	if (ci->type)
		printf (
			"%scontent-type: %s/%s\n",
			p, ci->type->type ? ci->type->type : "(null)",
			ci->type->subtype ? ci->type->subtype : "(null)");
	else
		printf ("%scontent-type: <unset>\n", p);
	printf (
		"%scontent-transfer-encoding: %s\n",
		p, ci->encoding ? ci->encoding : "(null)");
	printf (
		"%scontent-description: %s\n",
		p, ci->description ? ci->description : "(null)");
	printf ("%ssize: %lu\n", p, (gulong) ci->size);
	ci = ci->childs;
	while (ci) {
		camel_content_info_dump (ci, depth + 1);
		ci = ci->next;
	}
}

void
camel_message_info_dump (CamelMessageInfo *info)
{
	if (info == NULL) {
		printf ("No message?\n");
		return;
	}

	printf ("Subject: %s\n", camel_message_info_subject (info));
	printf ("To: %s\n", camel_message_info_to (info));
	printf ("Cc: %s\n", camel_message_info_cc (info));
	printf ("mailing list: %s\n", camel_message_info_mlist (info));
	printf ("From: %s\n", camel_message_info_from (info));
	printf ("UID: %s\n", camel_message_info_uid (info));
	printf ("Flags: %04x\n", camel_message_info_flags (info));
	camel_content_info_dump (((CamelMessageInfoBase *) info)->content, 0);
}

/**
 * camel_folder_summary_lock:
 * @summary: a #CamelFolderSummary
 *
 * Locks @summary. Unlock it with camel_folder_summary_unlock().
 *
 * Since: 2.32
 **/
void
camel_folder_summary_lock (CamelFolderSummary *summary)
{
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	g_rec_mutex_lock (&summary->priv->summary_lock);
}

/**
 * camel_folder_summary_unlock:
 * @summary: a #CamelFolderSummary
 *
 * Unlocks @summary, previously locked with camel_folder_summary_lock().
 *
 * Since: 2.32
 **/
void
camel_folder_summary_unlock (CamelFolderSummary *summary)
{
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	g_rec_mutex_unlock (&summary->priv->summary_lock);
}

gint
bdata_extract_digit (/* const */ gchar **part)
{
	if (!part || !*part || !**part)
		return 0;

	if (**part == ' ')
		*part += 1;

	if (!**part)
		return 0;

	return strtoul (*part, part, 10);
}

/* expecting "digit-value", where digit is length of the value */
gchar *
bdata_extract_string (/* const */ gchar **part)
{
	gint len, has_len;
	gchar *val;

	len = bdata_extract_digit (part);

	/* might be a '-' sign */
	if (part && *part && **part)
		*part += 1;

	if (len <= 0 || !part || !*part || !**part)
		return g_strdup ("");

	if (!**part)
		return g_strdup ("");

	has_len = strlen (*part);
	if (has_len < len)
		len = has_len;

	val = g_strndup (*part, len);
	*part += len;

	return val;
}

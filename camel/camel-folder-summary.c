/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <glib-object.h>
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

	GStaticRecMutex summary_lock;	/* for the summary hashtable/array */
	GStaticRecMutex io_lock;	/* load/save lock, for access to saved_count, etc */
	GStaticRecMutex filter_lock;	/* for accessing any of the filtering/indexing stuff, since we share them */
	GStaticRecMutex alloc_lock;	/* for setting up and using allocators */
	GStaticRecMutex ref_lock;	/* for reffing/unreffing messageinfo's ALWAYS obtain before summary_lock */
	GHashTable *flag_cache;

	gboolean need_preview;
	GHashTable *preview_updates;
};

static GStaticMutex info_lock = G_STATIC_MUTEX_INIT;

/* this lock is ONLY for the standalone messageinfo stuff */
#define GLOBAL_INFO_LOCK(i) g_static_mutex_lock(&info_lock)
#define GLOBAL_INFO_UNLOCK(i) g_static_mutex_unlock(&info_lock)

/* this should probably be conditional on it existing */
#define USE_BSEARCH

#define d(x)
#define io(x)			/* io debug */
#define w(x)

#define CAMEL_FOLDER_SUMMARY_VERSION (14)

#define META_SUMMARY_SUFFIX_LEN 5 /* strlen("-meta") */

#define EXTRACT_FIRST_STRING(val) len=strtoul (part, &part, 10); if (*part) part++; val=g_strndup (part, len); part+=len;
#define EXTRACT_STRING(val) if (*part) part++; len=strtoul (part, &part, 10); if (*part) part++; val=g_strndup (part, len); part+=len;
#define EXTRACT_FIRST_DIGIT(val) val=strtoul (part, &part, 10);
#define EXTRACT_DIGIT(val) if (*part && *part == ' ') part++; val=strtoul (part, &part, 10);

/* trivial lists, just because ... */
struct _node {
	struct _node *next;
};

static void cfs_schedule_info_release_timer (CamelFolderSummary *s);

static struct _node *my_list_append(struct _node **list, struct _node *n);
static gint my_list_size(struct _node **list);

static gint summary_header_load(CamelFolderSummary *, FILE *);
static gint summary_header_save(CamelFolderSummary *, FILE *);

static CamelMessageInfo * message_info_new_from_header(CamelFolderSummary *, struct _camel_header_raw *);
static CamelMessageInfo * message_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageInfo * message_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg, const gchar *bodystructure);
static CamelMessageInfo * message_info_migrate(CamelFolderSummary *, FILE *);
static void		  message_info_free(CamelFolderSummary *, CamelMessageInfo *);

static CamelMessageContentInfo * content_info_new_from_header(CamelFolderSummary *, struct _camel_header_raw *);
static CamelMessageContentInfo * content_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageContentInfo * content_info_new_from_message(CamelFolderSummary *s, CamelMimePart *mp);
static CamelMessageContentInfo * content_info_migrate(CamelFolderSummary *, FILE *);
static void			 content_info_free(CamelFolderSummary *, CamelMessageContentInfo *);

static gint save_message_infos_to_db (CamelFolderSummary *s, gboolean fresh_mir, GError **error);
static gint camel_read_mir_callback (gpointer  ref, gint ncol, gchar ** cols, gchar ** name);

static gchar *next_uid_string(CamelFolderSummary *s);

static CamelMessageContentInfo * summary_build_content_info(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimeParser *mp);
static CamelMessageContentInfo * summary_build_content_info_message(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimePart *object);

static CamelMessageInfo * message_info_from_uid (CamelFolderSummary *s, const gchar *uid);

G_DEFINE_TYPE (CamelFolderSummary, camel_folder_summary, CAMEL_TYPE_OBJECT)

static void
free_o_name(gpointer key, gpointer value, gpointer data)
{
	g_object_unref (value);
	g_free(key);
}

static void
folder_summary_dispose (GObject *object)
{
	CamelFolderSummaryPrivate *priv;

	priv = CAMEL_FOLDER_SUMMARY_GET_PRIVATE (object);

	if (priv->filter_index != NULL) {
		g_object_unref (priv->filter_index);
		priv->filter_index = NULL;
	}

	if (priv->filter_64 != NULL) {
		g_object_unref (priv->filter_64);
		priv->filter_64 = NULL;
	}

	if (priv->filter_qp != NULL) {
		g_object_unref (priv->filter_qp);
		priv->filter_qp = NULL;
	}

	if (priv->filter_uu != NULL) {
		g_object_unref (priv->filter_uu);
		priv->filter_uu = NULL;
	}

	if (priv->filter_save != NULL) {
		g_object_unref (priv->filter_save);
		priv->filter_save = NULL;
	}

	if (priv->filter_html != NULL) {
		g_object_unref (priv->filter_html);
		priv->filter_html = NULL;
	}

	if (priv->filter_stream != NULL) {
		g_object_unref (priv->filter_stream);
		priv->filter_stream = NULL;
	}

	if (priv->index != NULL) {
		g_object_unref (priv->index);
		priv->index = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_folder_summary_parent_class)->dispose (object);
}

static void
folder_summary_finalize (GObject *object)
{
	CamelFolderSummary *summary = CAMEL_FOLDER_SUMMARY (object);

	g_hash_table_destroy (summary->priv->flag_cache);
	if (summary->timeout_handle)
		g_source_remove (summary->timeout_handle);
	/*camel_folder_summary_clear(s);*/
	g_ptr_array_foreach (summary->uids, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (summary->uids, TRUE);
	g_hash_table_destroy (summary->loaded_infos);

	g_hash_table_foreach(summary->priv->filter_charset, free_o_name, NULL);
	g_hash_table_destroy(summary->priv->filter_charset);

	g_hash_table_destroy (summary->priv->preview_updates);

	g_free(summary->summary_path);

	/* Freeing memory occupied by meta-summary-header */
	g_free(summary->meta_summary->path);
	g_free(summary->meta_summary);

	g_static_rec_mutex_free (&summary->priv->summary_lock);
	g_static_rec_mutex_free (&summary->priv->io_lock);
	g_static_rec_mutex_free (&summary->priv->filter_lock);
	g_static_rec_mutex_free (&summary->priv->alloc_lock);
	g_static_rec_mutex_free (&summary->priv->ref_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_folder_summary_parent_class)->finalize (object);
}

static	gint
summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *record)
{
	io(printf("Loading header from db \n"));

	s->version = record->version;

	/* We may not worry, as we are setting a new standard here */
#if 0
	/* Legacy version check, before version 12 we have no upgrade knowledge */
	if ((s->version > 0xff) && (s->version & 0xff) < 12) {
		io(printf ("Summary header version mismatch"));
		errno = EINVAL;
		return -1;
	}

	if (!(s->version < 0x100 && s->version >= 13))
		io(printf("Loading legacy summary\n"));
	else
		io(printf("loading new-format summary\n"));
#endif

	s->flags = record->flags;
	s->nextuid = record->nextuid;
	s->time = record->time;
	s->saved_count = record->saved_count;

	s->unread_count = record->unread_count;
	s->deleted_count = record->deleted_count;
	s->junk_count = record->junk_count;
	s->visible_count = record->visible_count;
	s->junk_not_deleted_count = record->jnd_count;

	return 0;
}

static	CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s,
                      GError **error)
{
	CamelFIRecord * record = g_new0 (CamelFIRecord, 1);
	CamelStore *parent_store;
	CamelDB *db;
	const gchar *table_name;

	/* Though we are going to read, we do this during write,
	 * so lets use it that way. */
	table_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);
	db = parent_store->cdb_w;

	io(printf("Savining header to db\n"));

	record->folder_name = g_strdup (table_name);

	/* we always write out the current version */
	record->version = CAMEL_FOLDER_SUMMARY_VERSION;
	record->flags  = s->flags;
	record->nextuid = s->nextuid;
	record->time = s->time;

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

	s->unread_count = record->unread_count;
	s->deleted_count = record->deleted_count;
	s->junk_count = record->junk_count;
	s->visible_count = record->visible_count;
	s->junk_not_deleted_count = record->jnd_count;

	return record;
}

static CamelMessageInfo *
message_info_from_db (CamelFolderSummary *s, CamelMIRecord *record)
{
	CamelMessageInfoBase *mi;
	gint i;
	gint count;
	gchar *part, *label;

	mi = (CamelMessageInfoBase *)camel_message_info_new(s);

	io(printf("Loading message info from db\n"));

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
		EXTRACT_FIRST_DIGIT (mi->message_id.id.part.hi)
		EXTRACT_DIGIT (mi->message_id.id.part.lo)
		EXTRACT_DIGIT (count)

		if (count > 0) {
			mi->references = g_malloc(sizeof(*mi->references) + ((count-1) * sizeof(mi->references->references[0])));
			mi->references->size = count;
			for (i=0;i<count;i++) {
				EXTRACT_DIGIT (mi->references->references[i].id.part.hi)
				EXTRACT_DIGIT (mi->references->references[i].id.part.lo)
			}
		} else
			mi->references = NULL;

	}

	/* Extract User flags/labels */
	part = record->labels;
	if (part) {
		label = part;
		for (i=0;part[i];i++) {

			if (part[i] == ' ') {
				part[i] = 0;
				camel_flag_set(&mi->user_flags, label, TRUE);
				label = &(part[i+1]);
			}
		}
		camel_flag_set(&mi->user_flags, label, TRUE);
	}

	/* Extract User tags */
	part = record->usertags;
	EXTRACT_FIRST_DIGIT (count)
	for (i=0;i<count;i++) {
		gint len;
		gchar *name, *value;
		EXTRACT_STRING (name)
		EXTRACT_STRING (value)
		camel_tag_set(&mi->user_tags, name, value);
		g_free(name);
		g_free(value);
	}

	return (CamelMessageInfo *) mi;
}

static CamelMIRecord *
message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelMIRecord *record = g_new0(CamelMIRecord, 1);
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *) info;
	GString *tmp;
	CamelFlag *flag;
	CamelTag *tag;
	gint count, i;

	/* Assume that we dont have to take care of DB Safeness. It will be done while doing the DB transaction */
	record->uid = (gchar *) camel_pstring_strdup(camel_message_info_uid(mi));
	record->flags = mi->flags;

	record->read =  ((mi->flags & (CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK))) ? 1 : 0;
	record->deleted = mi->flags & CAMEL_MESSAGE_DELETED ? 1 : 0;
	record->replied = mi->flags & CAMEL_MESSAGE_ANSWERED ? 1 : 0;
	record->important = mi->flags & CAMEL_MESSAGE_FLAGGED ? 1 : 0;
	record->junk = mi->flags & CAMEL_MESSAGE_JUNK ? 1 : 0;
	record->dirty = mi->flags & CAMEL_MESSAGE_FOLDER_FLAGGED ? 1 : 0;
	record->attachment = mi->flags & CAMEL_MESSAGE_ATTACHMENTS ? 1 : 0;

	record->size = mi->size;
	record->dsent = mi->date_sent;
	record->dreceived = mi->date_received;

	record->subject = (gchar *) camel_pstring_strdup(camel_message_info_subject (mi));
	record->from = (gchar *) camel_pstring_strdup(camel_message_info_from (mi));
	record->to = (gchar *) camel_pstring_strdup(camel_message_info_to (mi));
	record->cc = (gchar *) camel_pstring_strdup(camel_message_info_cc (mi));
	record->mlist = (gchar *) camel_pstring_strdup(camel_message_info_mlist (mi));

	record->followup_flag = (gchar *) camel_pstring_strdup(camel_message_info_user_tag(info, "follow-up"));
	record->followup_completed_on = (gchar *) camel_pstring_strdup(camel_message_info_user_tag(info, "completed-on"));
	record->followup_due_by = (gchar *) camel_pstring_strdup(camel_message_info_user_tag(info, "due-by"));

	record->bodystructure = mi->bodystructure ? g_strdup (mi->bodystructure) : NULL;

	tmp = g_string_new (NULL);
	if (mi->references) {
		g_string_append_printf (tmp, "%lu %lu %lu", (gulong)mi->message_id.id.part.hi, (gulong)mi->message_id.id.part.lo, (gulong)mi->references->size);
		for (i=0;i<mi->references->size;i++)
			g_string_append_printf (tmp, " %lu %lu", (gulong)mi->references->references[i].id.part.hi, (gulong)mi->references->references[i].id.part.lo);
	} else {
		g_string_append_printf (tmp, "%lu %lu %lu", (gulong)mi->message_id.id.part.hi, (gulong)mi->message_id.id.part.lo, (gulong) 0);
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
	count = camel_tag_list_size(&mi->user_tags);
	g_string_append_printf (tmp, "%lu", (gulong)count);
	tag = mi->user_tags;
	while (tag) {
		/* FIXME: Should we handle empty tags? Can it be empty? If it potential crasher ahead*/
		g_string_append_printf (tmp, " %lu-%s %lu-%s", (gulong)strlen(tag->name), tag->name, (gulong)strlen(tag->value), tag->value);
		tag = tag->next;
	}
	record->usertags = tmp->str;
	g_string_free (tmp, FALSE);

	return record;
}

static CamelMessageContentInfo *
content_info_from_db(CamelFolderSummary *s, CamelMIRecord *record)
{
	CamelMessageContentInfo *ci;
	gchar *type, *subtype;
	guint32 count, i;
	CamelContentType *ct;
	gchar *part = record->cinfo;
	gint len;

	io(printf("Loading content info from db\n"));

	if (!part)
		return NULL;

	ci = camel_folder_summary_content_info_new(s);
	if (*part == ' ') part++; /* Move off the space in the record*/

	EXTRACT_FIRST_STRING (type)
	EXTRACT_STRING (subtype)
	ct = camel_content_type_new(type, subtype);
	g_free(type);		/* can this be removed? */
	g_free(subtype);
	EXTRACT_DIGIT (count)

	for (i = 0; i < count; i++) {
		gchar *name, *value;
		EXTRACT_STRING (name)
		EXTRACT_STRING (value)

		camel_content_type_set_param(ct, name, value);
		/* TODO: do this so we dont have to double alloc/free */
		g_free(name);
		g_free(value);
	}
	ci->type = ct;

	/* FIXME[disk-summary] move all these to camel pstring */
	EXTRACT_STRING (ci->id);
	EXTRACT_STRING (ci->description)
	EXTRACT_STRING (ci->encoding)
	EXTRACT_DIGIT (ci->size)

	record->cinfo = part; /* Keep moving the cursor in the record */

	ci->childs = NULL;

	return ci;
}

static gint
content_info_to_db(CamelFolderSummary *s, CamelMessageContentInfo *ci, CamelMIRecord *record)
{
	CamelContentType *ct;
	struct _camel_header_param *hp;
	GString *str = g_string_new (NULL);
	gchar *oldr;

	io(printf("Saving content info to db\n"));

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
		g_string_append_printf (str, " %d", my_list_size((struct _node **)&ct->params));
		hp = ct->params;
		while (hp) {
			if (hp->name)
				g_string_append_printf (str, " %d-%s", (gint)strlen(hp->name), hp->name);
			else
				g_string_append_printf (str, " 0-");
			if (hp->value)
				g_string_append_printf (str, " %d-%s", (gint)strlen (hp->value), hp->value);
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
		g_string_append_printf (str, " %d-%s", (gint)strlen (ci->id), ci->id);
	else
		g_string_append_printf (str, " 0-");
	if (ci->description)
		g_string_append_printf (str, " %d-%s", (gint)strlen (ci->description), ci->description);
	else
		g_string_append_printf (str, " 0-");
	if (ci->encoding)
		g_string_append_printf (str, " %d-%s", (gint)strlen (ci->encoding), ci->encoding);
	else
		g_string_append_printf (str, " 0-");
	g_string_append_printf (str, " %u", ci->size);

	if (record->cinfo) {
		oldr = record->cinfo;
		record->cinfo = g_strconcat(oldr, str->str, NULL);
		g_free (oldr); g_string_free (str, TRUE);
	} else {
		record->cinfo = str->str;
		g_string_free (str, FALSE);
	}

	return 0;
}

static gint
summary_header_save(CamelFolderSummary *s, FILE *out)
{
	gint unread = 0, deleted = 0, junk = 0, count, i;

	fseek(out, 0, SEEK_SET);

	io(printf("Savining header\n"));

	/* we always write out the current version */
	camel_file_util_encode_fixed_int32(out, CAMEL_FOLDER_SUMMARY_VERSION);
	camel_file_util_encode_fixed_int32(out, s->flags);
	camel_file_util_encode_fixed_int32(out, s->nextuid);
	camel_file_util_encode_time_t(out, s->time);

	count = camel_folder_summary_count(s);
	for (i=0; i<count; i++) {
		CamelMessageInfo *info = camel_folder_summary_index(s, i);
		guint32 flags;

		if (info == NULL)
			continue;

		flags = camel_message_info_flags(info);
		if ((flags & CAMEL_MESSAGE_SEEN) == 0)
			unread++;
		if ((flags & CAMEL_MESSAGE_DELETED) != 0)
			deleted++;
		if ((flags & CAMEL_MESSAGE_JUNK) != 0)
			junk++;

		camel_message_info_free(info);
	}

	camel_file_util_encode_fixed_int32(out, count);
	camel_file_util_encode_fixed_int32(out, unread);
	camel_file_util_encode_fixed_int32(out, deleted);

	return camel_file_util_encode_fixed_int32(out, junk);
}

static CamelMessageInfo *
message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
	CamelMessageInfoBase *to, *from = (CamelMessageInfoBase *)mi;
	CamelFlag *flag;
	CamelTag *tag;

	to = (CamelMessageInfoBase *)camel_message_info_new(s);

	to->flags = from->flags;
	to->size = from->size;
	to->date_sent = from->date_sent;
	to->date_received = from->date_received;
	to->refcount = 1;

	/* NB: We don't clone the uid */

	to->subject = camel_pstring_strdup(from->subject);
	to->from = camel_pstring_strdup(from->from);
	to->to = camel_pstring_strdup(from->to);
	to->cc = camel_pstring_strdup(from->cc);
	to->mlist = camel_pstring_strdup(from->mlist);
	memcpy(&to->message_id, &from->message_id, sizeof(to->message_id));
	to->preview = g_strdup (from->preview);
	if (from->references) {
		gint len = sizeof(*from->references) + ((from->references->size-1) * sizeof(from->references->references[0]));

		to->references = g_malloc(len);
		memcpy(to->references, from->references, len);
	}

	flag = from->user_flags;
	while (flag) {
		camel_flag_set(&to->user_flags, flag->name, TRUE);
		flag = flag->next;
	}

	tag = from->user_tags;
	while (tag) {
		camel_tag_set(&to->user_tags, tag->name, tag->value);
		tag = tag->next;
	}

	if (from->content) {
		/* FIXME: copy content-infos */
	}

	return (CamelMessageInfo *)to;
}

static gconstpointer
info_ptr(const CamelMessageInfo *mi, gint id)
{
	switch (id) {
		case CAMEL_MESSAGE_INFO_SUBJECT:
			return ((const CamelMessageInfoBase *)mi)->subject;
		case CAMEL_MESSAGE_INFO_FROM:
			return ((const CamelMessageInfoBase *)mi)->from;
		case CAMEL_MESSAGE_INFO_TO:
			return ((const CamelMessageInfoBase *)mi)->to;
		case CAMEL_MESSAGE_INFO_CC:
			return ((const CamelMessageInfoBase *)mi)->cc;
		case CAMEL_MESSAGE_INFO_MLIST:
			return ((const CamelMessageInfoBase *)mi)->mlist;
		case CAMEL_MESSAGE_INFO_MESSAGE_ID:
			return &((const CamelMessageInfoBase *)mi)->message_id;
		case CAMEL_MESSAGE_INFO_REFERENCES:
			return ((const CamelMessageInfoBase *)mi)->references;
		case CAMEL_MESSAGE_INFO_USER_FLAGS:
			return ((const CamelMessageInfoBase *)mi)->user_flags;
		case CAMEL_MESSAGE_INFO_USER_TAGS:
			return ((const CamelMessageInfoBase *)mi)->user_tags;
		case CAMEL_MESSAGE_INFO_HEADERS:
			return ((const CamelMessageInfoBase *)mi)->headers;
		case CAMEL_MESSAGE_INFO_CONTENT:
			return ((const CamelMessageInfoBase *)mi)->content;
		case CAMEL_MESSAGE_INFO_PREVIEW:
			return ((const CamelMessageInfoBase *)mi)->preview;
		default:
			g_return_val_if_reached (NULL);
	}
}

static guint32
info_uint32(const CamelMessageInfo *mi, gint id)
{
	switch (id) {
		case CAMEL_MESSAGE_INFO_FLAGS:
			return ((const CamelMessageInfoBase *)mi)->flags;
		case CAMEL_MESSAGE_INFO_SIZE:
			return ((const CamelMessageInfoBase *)mi)->size;
		default:
			g_return_val_if_reached (0);
	}
}

static time_t
info_time(const CamelMessageInfo *mi, gint id)
{
	switch (id) {
		case CAMEL_MESSAGE_INFO_DATE_SENT:
			return ((const CamelMessageInfoBase *)mi)->date_sent;
		case CAMEL_MESSAGE_INFO_DATE_RECEIVED:
			return ((const CamelMessageInfoBase *)mi)->date_received;
		default:
			g_return_val_if_reached (0);
	}
}

static gboolean
info_user_flag(const CamelMessageInfo *mi, const gchar *id)
{
	return camel_flag_get(&((CamelMessageInfoBase *)mi)->user_flags, id);
}

static const gchar *
info_user_tag(const CamelMessageInfo *mi, const gchar *id)
{
	return camel_tag_get(&((CamelMessageInfoBase *)mi)->user_tags, id);
}

static gboolean
info_set_user_flag(CamelMessageInfo *info, const gchar *name, gboolean value)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;
	gint res;

	res = camel_flag_set(&mi->user_flags, name, value);

	/* TODO: check this item is still in the summary first */
	if (mi->summary && res && mi->summary->folder && mi->uid) {
		CamelFolderChangeInfo *changes = camel_folder_change_info_new();

		mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		mi->dirty = TRUE;
		camel_folder_summary_touch(mi->summary);
		camel_folder_change_info_change_uid(changes, camel_message_info_uid(info));
		camel_folder_changed (mi->summary->folder, changes);
		camel_folder_change_info_free(changes);
	}

	return res;
}

static gboolean
info_set_user_tag(CamelMessageInfo *info, const gchar *name, const gchar *value)
{
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;
	gint res;

	res = camel_tag_set(&mi->user_tags, name, value);

	if (mi->summary && res && mi->summary->folder && mi->uid) {
		CamelFolderChangeInfo *changes = camel_folder_change_info_new();

		mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		mi->dirty = TRUE;
		camel_folder_summary_touch(mi->summary);
		camel_folder_change_info_change_uid(changes, camel_message_info_uid(info));
		camel_folder_changed (mi->summary->folder, changes);
		camel_folder_change_info_free(changes);
	}

	return res;
}

static gboolean
info_set_flags(CamelMessageInfo *info, guint32 flags, guint32 set)
{
	guint32 old;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;
	gint read=0, deleted=0, junk=0;
	/* TODO: locking? */

	if (flags & CAMEL_MESSAGE_SEEN && ((set & CAMEL_MESSAGE_SEEN) != (mi->flags & CAMEL_MESSAGE_SEEN)))
	{ read = set & CAMEL_MESSAGE_SEEN ? 1 : -1; d(printf("Setting read as %d\n", set & CAMEL_MESSAGE_SEEN ? 1 : 0));}

	if (flags & CAMEL_MESSAGE_DELETED && ((set & CAMEL_MESSAGE_DELETED) != (mi->flags & CAMEL_MESSAGE_DELETED)))
	{ deleted = set & CAMEL_MESSAGE_DELETED ? 1 : -1; d(printf("Setting deleted as %d\n", set & CAMEL_MESSAGE_DELETED ? 1 : 0));}

	if (flags & CAMEL_MESSAGE_JUNK && ((set & CAMEL_MESSAGE_JUNK) != (mi->flags & CAMEL_MESSAGE_JUNK)))
	{ junk = set & CAMEL_MESSAGE_JUNK ? 1 : -1; d(printf("Setting junk as %d\n", set & CAMEL_MESSAGE_JUNK ? 1 : 0));}

	old = mi->flags;
	mi->flags = (old & ~flags) | (set & flags);
	if (old != mi->flags) {
		mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		mi->dirty = TRUE;
		if (mi->summary)
			camel_folder_summary_touch(mi->summary);
	}

	if (((old & ~CAMEL_MESSAGE_SYSTEM_MASK) == (mi->flags & ~CAMEL_MESSAGE_SYSTEM_MASK)) && !((set & CAMEL_MESSAGE_JUNK_LEARN) && !(set & CAMEL_MESSAGE_JUNK)))
		return FALSE;

	if (mi->summary) {
		if (read && junk == 0 && !(mi->flags & CAMEL_MESSAGE_JUNK))
			mi->summary->unread_count -= read;
		else if (junk > 0)
			mi->summary->unread_count -= (old & CAMEL_MESSAGE_SEEN) ? 0 : 1;
		else if (junk < 0)
			mi->summary->unread_count -= (old & CAMEL_MESSAGE_SEEN) ? 0 : -1;

		if (deleted)
			mi->summary->deleted_count += deleted;
		if (junk)
			mi->summary->junk_count += junk;
		if (junk && !deleted)
			mi->summary->junk_not_deleted_count += junk;
		else if ((mi->flags & CAMEL_MESSAGE_JUNK) && deleted)
			mi->summary->junk_not_deleted_count -= deleted;

		if (((junk && !(mi->flags & CAMEL_MESSAGE_DELETED))) || (deleted && !(mi->flags & CAMEL_MESSAGE_JUNK)))
			mi->summary->visible_count -= junk ? junk : deleted;
	}
	if (mi->uid)
		g_hash_table_replace (CAMEL_FOLDER_SUMMARY_GET_PRIVATE(mi->summary)->flag_cache, (gchar *)mi->uid, GUINT_TO_POINTER(mi->flags));
	if (mi->summary && mi->summary->folder && mi->uid) {
		CamelFolderChangeInfo *changes = camel_folder_change_info_new();

		camel_folder_change_info_change_uid(changes, camel_message_info_uid(info));
		camel_folder_changed (mi->summary->folder, changes);
		camel_folder_change_info_free(changes);
	}

	d(printf("%d %d %d %d %d\n", mi->summary->unread_count, mi->summary->deleted_count, mi->summary->junk_count, mi->summary->junk_not_deleted_count, mi->summary->visible_count));
	return TRUE;
}

static void
camel_folder_summary_class_init (CamelFolderSummaryClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelFolderSummaryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = folder_summary_dispose;
	object_class->finalize = folder_summary_finalize;

	class->message_info_size = sizeof (CamelMessageInfoBase);
	class->content_info_size = sizeof (CamelMessageContentInfo);

	class->summary_header_load = summary_header_load;
	class->summary_header_save = summary_header_save;

	class->summary_header_from_db = summary_header_from_db;
	class->summary_header_to_db = summary_header_to_db;
	class->message_info_from_db = message_info_from_db;
	class->message_info_to_db = message_info_to_db;
	class->content_info_from_db = content_info_from_db;
	class->content_info_to_db = content_info_to_db;

	class->message_info_new_from_header  = message_info_new_from_header;
	class->message_info_new_from_parser = message_info_new_from_parser;
	class->message_info_new_from_message = message_info_new_from_message;
	class->message_info_migrate = message_info_migrate;
	class->message_info_free = message_info_free;
	class->message_info_clone = message_info_clone;
	class->message_info_from_uid = message_info_from_uid;

	class->content_info_new_from_header  = content_info_new_from_header;
	class->content_info_new_from_parser = content_info_new_from_parser;
	class->content_info_new_from_message = content_info_new_from_message;
	class->content_info_migrate = content_info_migrate;
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

}

static void
camel_folder_summary_init (CamelFolderSummary *summary)
{
	summary->priv = CAMEL_FOLDER_SUMMARY_GET_PRIVATE (summary);

	summary->priv->filter_charset = g_hash_table_new (
		camel_strcase_hash, camel_strcase_equal);

	summary->priv->flag_cache = g_hash_table_new (g_str_hash, g_str_equal);

	summary->message_info_chunks = NULL;
	summary->content_info_chunks = NULL;
	summary->priv->need_preview = FALSE;
	summary->priv->preview_updates = g_hash_table_new (g_str_hash, g_str_equal);
#if defined (DOESTRV) || defined (DOEPOOLV)
	summary->message_info_strings = CAMEL_MESSAGE_INFO_LAST;
#endif

	summary->version = CAMEL_FOLDER_SUMMARY_VERSION;
	summary->flags = 0;
	summary->time = 0;
	summary->nextuid = 1;

	summary->uids = g_ptr_array_new ();
	summary->loaded_infos = g_hash_table_new (g_str_hash, g_str_equal);

	g_static_rec_mutex_init (&summary->priv->summary_lock);
	g_static_rec_mutex_init (&summary->priv->io_lock);
	g_static_rec_mutex_init (&summary->priv->filter_lock);
	g_static_rec_mutex_init (&summary->priv->alloc_lock);
	g_static_rec_mutex_init (&summary->priv->ref_lock);

	summary->meta_summary = g_malloc0(sizeof(CamelFolderMetaSummary));

	/* Default is 20, any implementor having UIDs that has length
	   exceeding 20, has to override this value
	*/
	summary->meta_summary->uid_len = 20;
	summary->cache_load_time = 0;
	summary->timeout_handle = 0;
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
camel_folder_summary_new (struct _CamelFolder *folder)
{
	CamelFolderSummary *new;

	new = g_object_new (CAMEL_TYPE_FOLDER_SUMMARY, NULL);
	new->folder = folder;

	return new;
}

/**
 * camel_folder_summary_set_filename:
 * @summary: a #CamelFolderSummary object
 * @filename: a filename
 *
 * Set the filename where the summary will be loaded to/saved from.
 **/
void
camel_folder_summary_set_filename(CamelFolderSummary *s, const gchar *name)
{
	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	g_free(s->summary_path);
	s->summary_path = g_strdup(name);

	g_free(s->meta_summary->path);
	s->meta_summary->path = g_strconcat(name, "-meta", NULL);

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
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
camel_folder_summary_set_index(CamelFolderSummary *s, CamelIndex *index)
{
	struct _CamelFolderSummaryPrivate *p = CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s);

	if (p->index)
		g_object_unref (p->index);

	p->index = index;
	if (index)
		g_object_ref (index);
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
camel_folder_summary_set_build_content(CamelFolderSummary *s, gboolean state)
{
	s->build_content = state;
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
camel_folder_summary_count(CamelFolderSummary *s)
{
	return s->uids->len;
}

/**
 * camel_folder_summary_index:
 * @summary: a #CamelFolderSummary object
 * @index: item index
 *
 * Retrieve a summary item by index number.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 *
 * Returns: the summary item, or %NULL if @index is out of range
 **/
CamelMessageInfo *
camel_folder_summary_index (CamelFolderSummary *s, gint i)
{
	CamelMessageInfo *info = NULL;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);

	if (i < s->uids->len) {
		gchar *uid;
		uid = g_ptr_array_index (s->uids, i);

		/* FIXME: Get exception from caller
		and pass it on below */

		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

		return camel_folder_summary_uid (s, uid);
	}

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return info;
}

/* FIXME[disk-summary] Implement - camel_folder_summary_uid_exist -
 * directly through db than manual strcmp */

/**
 * camel_folder_summary_uid_from_index:
 * @s: a #CamelFolderSummary object
 * @i: item index
 *
 * Retrieve a summary item's uid  by index number.
 *
 * A newly allocated uid is returned, which must be
 * free'd as appropriate.
 *
 * Returns: the summary item's uid , or %NULL if @index is out of range
 *
 * Since: 2.24
 **/
gchar *
camel_folder_summary_uid_from_index (CamelFolderSummary *s, gint i)
{
	gchar *uid=NULL;
	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	if (i<s->uids->len)
		uid = g_strdup (g_ptr_array_index(s->uids, i));

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return uid;

}

/**
 * camel_folder_summary_check_uid
 * @s: a #CamelFolderSummary object
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
camel_folder_summary_check_uid (CamelFolderSummary *s, const gchar *uid)
{
	gboolean ret = FALSE;
	gint i;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	for (i=0; i<s->uids->len; i++) {
		if (strcmp(s->uids->pdata[i], uid) == 0) {
			camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
			return TRUE;
		}
	}

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return ret;
}

/**
 * camel_folder_summary_array:
 * @summary: a #CamelFolderSummary object
 *
 * Obtain a copy of the summary array.  This is done atomically,
 * so cannot contain empty entries.
 *
 * It must be freed using g_ptr_array_free
 *
 * Returns: a #GPtrArray of uids
 **/
GPtrArray *
camel_folder_summary_array(CamelFolderSummary *s)
{
	GPtrArray *res = g_ptr_array_new();
	gint i;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	g_ptr_array_set_size(res, s->uids->len);
	for (i=0;i<s->uids->len;i++)
		res->pdata[i] = (gpointer) camel_pstring_strdup ((gchar *)g_ptr_array_index(s->uids, i));

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return res;
}

/**
 * camel_folder_summary_get_hashtable:
 * @summary: a #CamelFolderSummary object
 *
 * Obtain a copy of the summary array in the hashtable.  This is done atomically,
 * so cannot contain empty entries.
 *
 * It must be freed using camel_folder_summary_free_hashtable
 *
 * Returns: a #GHashTable of uids
 *
 * Since: 2.26
 **/
GHashTable *
camel_folder_summary_get_hashtable(CamelFolderSummary *s)
{
	GHashTable *hash = g_hash_table_new (g_str_hash, g_str_equal);
	gint i;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	for (i=0;i<s->uids->len;i++)
		g_hash_table_insert (hash, (gpointer)camel_pstring_strdup ((gchar *)g_ptr_array_index(s->uids, i)), GINT_TO_POINTER(1));

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return hash;
}

/**
 * camel_folder_summary_free_hashtable:
 *
 * Since: 2.26
 **/
void
camel_folder_summary_free_hashtable (GHashTable *ht)
{
	g_hash_table_foreach (ht, (GHFunc)camel_pstring_free, NULL);
	g_hash_table_destroy (ht);
}

/**
 * camel_folder_summary_peek_info:
 *
 * Since: 2.26
 **/
CamelMessageInfo *
camel_folder_summary_peek_info (CamelFolderSummary *s, const gchar *uid)
{
	CamelMessageInfo *info = g_hash_table_lookup(s->loaded_infos, uid);

	if (info)
		camel_message_info_ref(info);
	return info;
}

struct _db_pass_data {
	CamelFolderSummary *summary;
	gboolean add; /* or just insert to hashtable */
};

static CamelMessageInfo *
message_info_from_uid (CamelFolderSummary *s, const gchar *uid)
{
	CamelMessageInfo *info;
	gint ret;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	info = g_hash_table_lookup (s->loaded_infos, uid);

	if (!info) {
		CamelDB *cdb;
		CamelStore *parent_store;
		const gchar *folder_name;
		struct _db_pass_data data;

		d(printf ("\ncamel_folder_summary_uid called \n"));
		s->flags &= ~CAMEL_SUMMARY_DIRTY;

		folder_name = camel_folder_get_full_name (s->folder);
		parent_store = camel_folder_get_parent_store (s->folder);
		cdb = parent_store->cdb_r;

		data.summary = s;
		data.add = FALSE;

		ret = camel_db_read_message_info_record_with_uid (
			cdb, folder_name, uid, &data,
			camel_read_mir_callback, NULL);
		if (ret != 0) {
			camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
			return NULL;
		}

		/* We would have double reffed at camel_read_mir_callback */
		info = g_hash_table_lookup (s->loaded_infos, uid);

		cfs_schedule_info_release_timer (s);
	}

	if (info)
		camel_message_info_ref (info);

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return info;
}

/**
 * camel_folder_summary_uid:
 * @summary: a #CamelFolderSummary object
 * @uid: a uid
 *
 * Retrieve a summary item by uid.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 *
 * Returns: the summary item, or %NULL if the uid @uid is not available
 **/
CamelMessageInfo *
camel_folder_summary_uid (CamelFolderSummary *summary,
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
 * camel_folder_summary_next_uid:
 * @summary: a #CamelFolderSummary object
 *
 * Generate a new unique uid value as an integer.  This
 * may be used to create a unique sequence of numbers.
 *
 * Returns: the next unique uid value
 **/
guint32
camel_folder_summary_next_uid(CamelFolderSummary *s)
{
	guint32 uid;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	uid = s->nextuid++;

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	/* FIXME: sync this to disk */
/*	summary_header_save(s);*/
	return uid;
}

/**
 * camel_folder_summary_set_uid:
 * @summary: a #CamelFolderSummary object
 * @uid: The next minimum uid to assign.  To avoid clashing
 * uid's, set this to the uid of a given messages + 1.
 *
 * Set the next minimum uid available.  This can be used to
 * ensure new uid's do not clash with existing uid's.
 **/
void
camel_folder_summary_set_uid(CamelFolderSummary *s, guint32 uid)
{
	/* TODO: sync to disk? */
	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	s->nextuid = MAX(s->nextuid, uid);

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
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

static CamelMessageContentInfo *
perform_content_info_load_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	gint i;
	guint32 count;
	CamelMessageContentInfo *ci, *pci;
	gchar *part;

	ci = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->content_info_from_db (s, mir);
	if (ci == NULL)
		return NULL;
	part = mir->cinfo;
	if (!part)
		return ci;
	if (*part == ' ') part++;
	EXTRACT_DIGIT (count);

	mir->cinfo = part;
	for (i=0;i<count;i++) {
		pci = perform_content_info_load_from_db (s, mir);
		if (pci ) {
			my_list_append((struct _node **)&ci->childs, (struct _node *)pci);
			pci->parent = ci;
		} else {
			d(fprintf (stderr, "Summary file format messed up?"));
			camel_folder_summary_content_info_free (s, ci);
			return NULL;
		}
	}
	return ci;
}

/* loads the content descriptions, recursively */
static CamelMessageContentInfo *
perform_content_info_migrate(CamelFolderSummary *s, FILE *in)
{
	gint i;
	guint32 count;
	CamelMessageContentInfo *ci, *part;

	ci = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->content_info_migrate (s, in);
	if (ci == NULL)
		return NULL;

	if (camel_file_util_decode_uint32(in, &count) == -1) {
		camel_folder_summary_content_info_free (s, ci);
		return NULL;
	}

	for (i=0;i<count;i++) {
		part = perform_content_info_migrate(s, in);
		if (part) {
			my_list_append((struct _node **)&ci->childs, (struct _node *)part);
			part->parent = ci;
		} else {
			d(fprintf (stderr, "Summary file format messed up?"));
			camel_folder_summary_content_info_free (s, ci);
			return NULL;
		}
	}
	return ci;
}

static void
append_changed_uids (gchar *key, CamelMessageInfoBase *info, GPtrArray *array)
{
	if (info->dirty || info->flags & CAMEL_MESSAGE_FOLDER_FLAGGED)
		g_ptr_array_add (array, (gpointer)camel_pstring_strdup((camel_message_info_uid(info))));
}

/**
 * camel_folder_summary_get_changed:
 *
 * Since: 2.24
 **/
GPtrArray *
camel_folder_summary_get_changed (CamelFolderSummary *s)
{
	GPtrArray *res = g_ptr_array_new();

	/* FIXME[disk-summary] sucks, this function returns from memory.
	 * We need to have collate or something to get the modified ones
	 * from DB and merge */

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	g_hash_table_foreach (s->loaded_infos, (GHFunc) append_changed_uids, res);
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return res;
}

static void
count_changed_uids (gchar *key, CamelMessageInfoBase *info, gint *count)
{
	if (info->dirty)
		(*count)++;
}

static gint
cfs_count_dirty (CamelFolderSummary *s)
{
	gint count = 0;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	g_hash_table_foreach (s->loaded_infos, (GHFunc) count_changed_uids, &count);
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return count;
}

/* FIXME[disk-summary] I should have a better LRU algorithm  */
static gboolean
remove_item (gchar *key, CamelMessageInfoBase *info, GSList **to_free_list)
{
	d(printf("%d(%d)\t", info->refcount, info->dirty)); /* camel_message_info_dump (info); */
	if (info->refcount == 1 && !info->dirty && !(info->flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
		*to_free_list = g_slist_prepend (*to_free_list, info);
		return TRUE;
	}
	return FALSE;
}

struct _folder_summary_free_msg {
	CamelSessionThreadMsg msg;
	CamelFolderSummary *summary;
};

static void
remove_cache (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_summary_free_msg *m = (struct _folder_summary_free_msg *)msg;
	CamelFolderSummary *s = m->summary;
	GSList *to_free_list = NULL, *l;

	CAMEL_DB_RELEASE_SQLITE_MEMORY;

	if (time(NULL) - s->cache_load_time < SUMMARY_CACHE_DROP)
		return;

	/* FIXME[disk-summary] hack. fix it */
	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
	g_hash_table_foreach_remove  (s->loaded_infos, (GHRFunc) remove_item, &to_free_list);
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);

	/* Deferred freeing as _free function will try to remove
	   entries from the hash_table in foreach_remove otherwise */
	for (l = to_free_list; l; l = l->next)
		camel_message_info_free (l->data);
	g_slist_free (to_free_list);

	dd(printf("   done .. now %d\n", g_hash_table_size (s->loaded_infos)));
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	s->cache_load_time = time(NULL);
}

static void remove_cache_end (CamelSession *session, CamelSessionThreadMsg *msg)
{
		struct _folder_summary_free_msg *m = (struct _folder_summary_free_msg *)msg;
		g_object_unref (m->summary);
}

static CamelSessionThreadOps remove_timeout_ops = {
	remove_cache,
	remove_cache_end,
};

static gboolean
cfs_try_release_memory (CamelFolderSummary *s)
{
	struct _folder_summary_free_msg *m;
	CamelStore *parent_store;
	CamelSession *session;

	/* If folder is freed or if the cache is nil then clean up */
	if (!s->folder || !g_hash_table_size(s->loaded_infos)) {
		s->cache_load_time = 0;
		s->timeout_handle = 0;
		return FALSE;
	}

	parent_store = camel_folder_get_parent_store (s->folder);
	session = CAMEL_SERVICE (parent_store)->session;

	if (time(NULL) - s->cache_load_time < SUMMARY_CACHE_DROP)
		return TRUE;

	m = camel_session_thread_msg_new(session, &remove_timeout_ops, sizeof(*m));
	m->summary = g_object_ref (s);
	camel_session_thread_queue(session, &m->msg, 0);

	return TRUE;
}

static void
cfs_schedule_info_release_timer (CamelFolderSummary *s)
{
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (s));

	if (!s->timeout_handle) {
		static gboolean know_can_do = FALSE, can_do = TRUE;

		if (!know_can_do) {
			can_do = !g_getenv ("CAMEL_FREE_INFOS");
			know_can_do = TRUE;
		}

		/* FIXME[disk-summary] LRU please and not timeouts */
		if (can_do)
			s->timeout_handle = g_timeout_add_seconds (SUMMARY_CACHE_DROP, (GSourceFunc) cfs_try_release_memory, s);
	}

	/* update also cache load time to the actual, to not release something just loaded */
	s->cache_load_time = time (NULL);
}

static gint
cfs_cache_size (CamelFolderSummary *s)
{
	/* FIXME[disk-summary] this is a timely hack. fix it well */
	if (!CAMEL_IS_VEE_FOLDER(s->folder))
		return g_hash_table_size (s->loaded_infos);
	else
		return s->uids->len;
}

/* Update preview of cached messages */

struct _preview_update_msg {
	CamelSessionThreadMsg msg;

	CamelFolder *folder;
	GError *error;
};

static void
msg_update_preview (const gchar *uid, gpointer value, CamelFolder *folder)
{
	CamelMessageInfoBase *info = (CamelMessageInfoBase *)camel_folder_summary_uid (folder->summary, uid);
	CamelMimeMessage *msg;
	CamelStore *parent_store;
	const gchar *full_name;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	msg = camel_folder_get_message (folder, uid, NULL);
	if (msg != NULL) {
		if (camel_mime_message_build_preview ((CamelMimePart *)msg, (CamelMessageInfo *)info) && info->preview)
			camel_db_write_preview_record (parent_store->cdb_w, full_name, info->uid, info->preview, NULL);
	}
	camel_message_info_free(info);
}

static void
pick_uids (const gchar *uid, CamelMessageInfoBase *mi, GPtrArray *array)
{
	if (mi->preview)
		g_ptr_array_add (array, (gchar *)camel_pstring_strdup(uid));
}

static gboolean
fill_mi (const gchar *uid, const gchar *msg, CamelFolder *folder)
{
	CamelMessageInfoBase *info;

	info = g_hash_table_lookup (folder->summary->loaded_infos, uid);
	if (info) /* We re assign the memory of msg */
		info->preview = (gchar *)msg;
	camel_pstring_free (uid); /* unref the uid */

	return TRUE;
}

static void
preview_update_exec (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _preview_update_msg *m = (struct _preview_update_msg *)msg;
	/* FIXME: Either lock & use or copy & use.*/
	GPtrArray *uids_uncached= camel_folder_get_uncached_uids (m->folder, m->folder->summary->uids, NULL);
	GHashTable *hash = camel_folder_summary_get_hashtable (m->folder->summary);
	GHashTable *preview_data;
	CamelStore *parent_store;
	const gchar *full_name;
	gint i;

	full_name = camel_folder_get_full_name (m->folder);
	parent_store = camel_folder_get_parent_store (m->folder);
	preview_data = camel_db_get_folder_preview (parent_store->cdb_r, full_name, NULL);
	if (preview_data) {
		g_hash_table_foreach_remove (preview_data, (GHRFunc)fill_mi, m->folder);
		g_hash_table_destroy (preview_data);
	}

	camel_folder_summary_lock (m->folder->summary, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	g_hash_table_foreach (m->folder->summary->loaded_infos, (GHFunc)pick_uids, uids_uncached);
	camel_folder_summary_unlock (m->folder->summary, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	for (i=0; i < uids_uncached->len; i++) {
		g_hash_table_remove (hash, uids_uncached->pdata[i]);
		camel_pstring_free (uids_uncached->pdata[i]); /* unref the hash table key */
	}

	camel_folder_lock (m->folder, CAMEL_FOLDER_REC_LOCK);
	camel_db_begin_transaction (parent_store->cdb_w, NULL);
	g_hash_table_foreach (hash, (GHFunc)msg_update_preview, m->folder);
	camel_db_end_transaction (parent_store->cdb_w, NULL);
	camel_folder_unlock (m->folder, CAMEL_FOLDER_REC_LOCK);
	camel_folder_free_uids(m->folder, uids_uncached);
	camel_folder_summary_free_hashtable (hash);
}

static void
preview_update_free (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _preview_update_msg *m = (struct _preview_update_msg *)msg;

	m=m;
}
static CamelSessionThreadOps preview_update_ops = {
	preview_update_exec,
	preview_update_free,
};

/* end */

static gint
cfs_reload_from_db (CamelFolderSummary *s, GError **error)
{
	CamelDB *cdb;
	CamelStore *parent_store;
	const gchar *folder_name;
	gint ret = 0;
	struct _db_pass_data data;

	/* FIXME[disk-summary] baseclass this, and vfolders we may have to
	 * load better. */
	d(printf ("\ncamel_folder_summary_reload_from_db called \n"));

	folder_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);
	cdb = parent_store->cdb_r;

	/* FIXME FOR SANKAR: No need to pass the address of summary here. */
	data.summary = s;
	data.add = FALSE;
	ret = camel_db_read_message_info_records (cdb, folder_name, (gpointer)&data, camel_read_mir_callback, NULL);

	cfs_schedule_info_release_timer (s);

	if (CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s)->need_preview) {
		struct _preview_update_msg *m;

		m = camel_session_thread_msg_new(((CamelService *)parent_store)->session, &preview_update_ops, sizeof(*m));
		m->folder = s->folder;
		m->error = NULL;
		camel_session_thread_queue(((CamelService *)parent_store)->session, &m->msg, 0);
	}

	return ret == 0 ? 0 : -1;
}

/**
 * camel_folder_summary_add_preview:
 *
 * Since: 2.28
 **/
void
camel_folder_summary_add_preview (CamelFolderSummary *s, CamelMessageInfo *info)
{
	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	g_hash_table_insert (CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s)->preview_updates, (gchar *)info->uid, ((CamelMessageInfoBase *)info)->preview);
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
}

/**
 * camel_folder_summary_prepare_fetch_all:
 * @s: #CamelFolderSummary object
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
camel_folder_summary_prepare_fetch_all (CamelFolderSummary *s,
                                        GError **error)
{
	guint loaded, known;

	g_return_if_fail (s != NULL);

	loaded = cfs_cache_size (s);
	known = camel_folder_summary_count (s);

	if (known - loaded > 50) {
		camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
		cfs_reload_from_db (s, error);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	}

	/* update also cache load time, even when not loaded anything */
	s->cache_load_time = time (NULL);
}

/**
 * camel_folder_summary_get_flag_cache:
 *
 * Since: 2.26
 **/
GHashTable *
camel_folder_summary_get_flag_cache (CamelFolderSummary *summary)
{
	struct _CamelFolderSummaryPrivate *p = CAMEL_FOLDER_SUMMARY_GET_PRIVATE(summary);

	return p->flag_cache;
}

/**
 * camel_folder_summary_load_from_db:
 *
 * Since: 2.24
 **/
gint
camel_folder_summary_load_from_db (CamelFolderSummary *s,
                                   GError **error)
{
	CamelDB *cdb;
	CamelStore *parent_store;
	const gchar *full_name;
	gint ret = 0;
	struct _CamelFolderSummaryPrivate *p = CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s);
	GError *local_error = NULL;

	/* struct _db_pass_data data; */
	d(printf ("\ncamel_folder_summary_load_from_db called \n"));
	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	full_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);
	ret = camel_folder_summary_header_load_from_db (s, parent_store, full_name, error);

	if (ret)
		return ret;

	cdb = parent_store->cdb_r;

	ret = camel_db_get_folder_uids_flags (
		cdb, full_name, s->sort_by, s->collate,
		s->uids, p->flag_cache, &local_error);

	if (local_error != NULL && local_error->message != NULL &&
		strstr (local_error->message, "no such table") != NULL) {
		/* create table the first time it is accessed and missing */
		ret = camel_db_prepare_message_info_table (cdb, full_name, error);
	} else if (local_error != NULL)
		g_propagate_error (error, local_error);

	return ret == 0 ? 0 : -1;
}

/**
 * camel_folder_summary_sort_uids
 *
 * Sort the summary UIDS as per the folder requirements. This is mostly for backends use. 
 * They client would hardly have a need to call this function.
 **/
void
camel_folder_summary_sort_uids (CamelFolderSummary *s)
{
	struct _CamelFolderSummaryPrivate *p = CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s);

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	camel_folder_sort_uids (s->folder, s->uids);
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
}

static void
mir_from_cols (CamelMIRecord *mir, CamelFolderSummary *s, gint ncol, gchar ** cols, gchar ** name)
{
	gint i;

	for (i = 0; i < ncol; ++i) {

		if (!strcmp (name [i], "uid"))
			mir->uid = (gchar *) camel_pstring_strdup (cols[i]);
		else if (!strcmp (name [i], "flags"))
			mir->flags = cols[i] ? strtoul (cols[i], NULL, 10) : 0;
		else if (!strcmp (name [i], "read"))
			mir->read =  (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "deleted"))
			mir->deleted = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "replied"))
			mir->replied = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "important"))
			mir->important = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "junk"))
			mir->junk = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "attachment"))
			mir->attachment = (cols[i]) ? ( ((strtoul (cols[i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "size"))
			mir->size =  cols[i] ? strtoul (cols[i], NULL, 10) : 0;
		else if (!strcmp (name [i], "dsent"))
			mir->dsent = cols[i] ? strtol (cols[i], NULL, 10) : 0;
		else if (!strcmp (name [i], "dreceived"))
			mir->dreceived = cols[i] ? strtol (cols[i], NULL, 10) : 0;
		else if (!strcmp (name [i], "subject"))
			mir->subject = (gchar *) camel_pstring_strdup (cols[i]);
		else if (!strcmp (name [i], "mail_from"))
			mir->from = (gchar *) camel_pstring_strdup (cols[i]);
		else if (!strcmp (name [i], "mail_to"))
			mir->to = (gchar *) camel_pstring_strdup (cols[i]);
		else if (!strcmp (name [i], "mail_cc"))
			mir->cc = (gchar *) camel_pstring_strdup(cols[i]);
		else if (!strcmp (name [i], "mlist"))
			mir->mlist = (gchar *) camel_pstring_strdup (cols[i]);
		else if (!strcmp (name [i], "followup_flag"))
			mir->followup_flag = (gchar *) camel_pstring_strdup(cols[i]);
		else if (!strcmp (name [i], "followup_completed_on"))
			mir->followup_completed_on = (gchar *) camel_pstring_strdup(cols[i]);
		else if (!strcmp (name [i], "followup_due_by"))
			mir->followup_due_by = (gchar *) camel_pstring_strdup(cols[i]);
		else if (!strcmp (name [i], "part"))
			mir->part = g_strdup (cols[i]);
		else if (!strcmp (name [i], "labels"))
			mir->labels = g_strdup (cols[i]);
		else if (!strcmp (name [i], "usertags"))
			mir->usertags = g_strdup (cols[i]);
		else if (!strcmp (name [i], "cinfo"))
			mir->cinfo = g_strdup(cols[i]);
		else if (!strcmp (name [i], "bdata"))
			mir->bdata = g_strdup(cols[i]);
		/* Evolution itself doesn't yet use this, ignoring
		else if (!strcmp (name [i], "bodystructure"))
			mir->bodystructure = g_strdup(cols[i]); */

	}
}

static gint
camel_read_mir_callback (gpointer  ref, gint ncol, gchar ** cols, gchar ** name)
{
	struct _db_pass_data *data = (struct _db_pass_data *) ref;
	CamelFolderSummary *s = data->summary;
	CamelMIRecord *mir;
	CamelMessageInfo *info;
	gint ret = 0;

	mir = g_new0 (CamelMIRecord , 1);
	mir_from_cols (mir, s, ncol, cols, name);

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	if (g_hash_table_lookup (s->loaded_infos, mir->uid)) {
		/* Unlock and better return*/
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
		camel_db_camel_mir_free (mir);
		return ret;
	}
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	info = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->message_info_from_db (s, mir);

	if (info) {

		if (s->build_content) {
			gchar *tmp;
			tmp = mir->cinfo;
			/* FIXME: this should be done differently, how i don't know */
			((CamelMessageInfoBase *)info)->content = perform_content_info_load_from_db (s, mir);
			if (((CamelMessageInfoBase *)info)->content == NULL) {
				camel_message_info_free(info);
				info = NULL;
			}
			mir->cinfo = tmp;

			if (!info) {
				camel_db_camel_mir_free (mir);
				return -1;
			}
		}

		/* Just now we are reading from the DB, it can't be dirty. */
		((CamelMessageInfoBase *)info)->dirty = FALSE;
		if (data->add)
			camel_folder_summary_add (s, info);
		else
			camel_folder_summary_insert (s, info, TRUE);

	} else {
		g_warning ("Loading messageinfo from db failed");
		ret = -1;
	}

	camel_db_camel_mir_free (mir);

	return ret;
}

/**
 * camel_folder_summary_migrate_infos:
 *
 * Since: 2.24
 **/
gint
camel_folder_summary_migrate_infos(CamelFolderSummary *s)
{
	FILE *in;
	gint i;
	CamelMessageInfo *mi;
	CamelMessageInfoBase *info;
	CamelStore *parent_store;
	gint ret = 0;
	CamelDB *cdb;
	CamelFIRecord *record;

	parent_store = camel_folder_get_parent_store (s->folder);
	cdb = parent_store->cdb_w;

	/* Kick off the gc thread cycle. */
	if (s->timeout_handle)
		g_source_remove (s->timeout_handle);
	s->timeout_handle = 0;

	d(g_print ("\ncamel_folder_summary_load from FLAT FILE called \n"));

	if (s->summary_path == NULL) {
		g_warning ("No summary path set. Unable to migrate\n");
		return -1;
	}

	in = g_fopen(s->summary_path, "rb");
	if (in == NULL)
		return -1;

	if (CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->summary_header_load (s, in) == -1)
		goto error;

	/* now read in each message ... */
	for (i=0;i<s->saved_count;i++) {
		CamelTag *tag;

		mi = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->message_info_migrate (s, in);

		if (mi == NULL)
			goto error;

		/* FIXME: this should be done differently, how i don't know */
		if (s->build_content) {
			((CamelMessageInfoBase *)mi)->content = perform_content_info_migrate (s, in);
			if (((CamelMessageInfoBase *)mi)->content == NULL) {
				camel_message_info_free(mi);
				goto error;
			}
		}

		info = (CamelMessageInfoBase *)mi;
		tag = info->user_tags;
		while (tag) {
			if (strcmp (tag->name, "label")) {
				camel_flag_set(&info->user_flags, tag->value, TRUE);
			}
			tag = tag->next;
		}

		mi->dirty = TRUE;
		g_hash_table_insert (s->loaded_infos, (gpointer) mi->uid, mi);
	}

	if (fclose (in) != 0)
		return -1;

	record = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->summary_header_to_db (s, NULL);
	if (!record) {
		return -1;
	}

	ret = save_message_infos_to_db (s, TRUE, NULL);

	if (ret != 0) {
		return -1;
	}

	camel_db_begin_transaction (cdb, NULL);
	ret = camel_db_write_folder_info_record (cdb, record, NULL);
	camel_db_end_transaction (cdb, NULL);

	g_free (record->bdata);
	g_free (record);

	if (ret != 0) {
		return -1;
	}

	return ret;

error:
	if (errno != EINVAL)
		g_warning ("Cannot load summary file: '%s': %s", s->summary_path, g_strerror (errno));

	fclose (in);

	return -1;

}

/* saves the content descriptions, recursively */
static gint
perform_content_info_save_to_db (CamelFolderSummary *s, CamelMessageContentInfo *ci, CamelMIRecord *record)
{
	CamelMessageContentInfo *part;
	gchar *oldr;

	if (CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->content_info_to_db (s, ci, record) == -1)
		return -1;

	oldr = record->cinfo;
	record->cinfo = g_strdup_printf ("%s %d", oldr, my_list_size ((struct _node **)&ci->childs));
	g_free (oldr);

	part = ci->childs;
	while (part) {
		if (perform_content_info_save_to_db (s, part, record) == -1)
			return -1;
		part = part->next;
	}

	return 0;
}

typedef struct {
	GError **error;
	gboolean migration;
	gint progress;
} SaveToDBArgs;

static void
save_to_db_cb (gpointer key, gpointer value, gpointer data)
{
	SaveToDBArgs *args = (SaveToDBArgs *) data;
	GError **error = args->error;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)value;
	CamelFolderSummary *s = (CamelFolderSummary *)mi->summary;
	CamelStore *parent_store;
	const gchar *full_name;
	CamelDB *cdb;
	CamelMIRecord *mir;

	full_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);
	cdb = parent_store->cdb_w;

	if (!args->migration && !mi->dirty)
		return;

	mir = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->message_info_to_db (s, (CamelMessageInfo *) mi);

	if (mir && s->build_content) {
		if (perform_content_info_save_to_db (s, ((CamelMessageInfoBase *)mi)->content, mir) == -1) {
			g_warning ("unable to save mir+cinfo for uid: %s\n", mir->uid);
			camel_db_camel_mir_free (mir);
			/* FIXME: Add exception here */
			return;
		}
	}

	if (!args->migration) {
			if (camel_db_write_message_info_record (cdb, full_name, mir, error) != 0) {
					camel_db_camel_mir_free (mir);
					return;
			}
	} else {
			if (camel_db_write_fresh_message_info_record (cdb, CAMEL_DB_IN_MEMORY_TABLE, mir, error) != 0) {
					camel_db_camel_mir_free (mir);
					return;
			}

			if (args->progress > CAMEL_DB_IN_MEMORY_TABLE_LIMIT) {
			    g_print ("BULK INsert limit reached \n");
				camel_db_flush_in_memory_transactions (cdb, full_name, error);
				camel_db_start_in_memory_transactions (cdb, error);
				args->progress = 0;
			} else {
				args->progress++;
			}
	}

	/* Reset the dirty flag which decides if the changes are synced to the DB or not.
	The FOLDER_FLAGGED should be used to check if the changes are synced to the server.
	So, dont unset the FOLDER_FLAGGED flag */
	mi->dirty = FALSE;

	camel_db_camel_mir_free (mir);
}

static gint
save_message_infos_to_db (CamelFolderSummary *s,
                          gboolean fresh_mirs,
                          GError **error)
{
	CamelStore *parent_store;
	CamelDB *cdb;
	const gchar *full_name;
	SaveToDBArgs args;

	args.error = error;
	args.migration = fresh_mirs;
	args.progress = 0;

	full_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);
	cdb = parent_store->cdb_w;

	if (camel_db_prepare_message_info_table (cdb, full_name, error) != 0)
		return -1;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	/* Push MessageInfo-es */
	g_hash_table_foreach (s->loaded_infos, save_to_db_cb, &args);
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
/* FIXME[disk-summary] make sure we free the message infos that are loaded
 * are freed if not used anymore or should we leave that to the timer? */

	return 0;
}

static void
msg_save_preview (const gchar *uid, gpointer value, CamelFolder *folder)
{
	CamelStore *parent_store;
	const gchar *full_name;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	camel_db_write_preview_record (
		parent_store->cdb_w, full_name, uid, (gchar *)value, NULL);
}

/**
 * camel_folder_summary_save_to_db:
 *
 * Since: 2.24
 **/
gint
camel_folder_summary_save_to_db (CamelFolderSummary *s,
                                 GError **error)
{
	CamelStore *parent_store;
	CamelDB *cdb;
	CamelFIRecord *record;
	gint ret, count;

	parent_store = camel_folder_get_parent_store (s->folder);
	cdb = parent_store->cdb_w;

	d(printf ("\ncamel_folder_summary_save_to_db called \n"));
	if (CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s)->need_preview && g_hash_table_size(CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s)->preview_updates)) {
		camel_db_begin_transaction (parent_store->cdb_w, NULL);
		camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
		g_hash_table_foreach (CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s)->preview_updates, (GHFunc)msg_save_preview, s->folder);
		g_hash_table_remove_all (CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s)->preview_updates);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
		camel_db_end_transaction (parent_store->cdb_w, NULL);
	}

	if (!(s->flags & CAMEL_SUMMARY_DIRTY))
		return 0;

	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	count = cfs_count_dirty(s);
	if (!count)
		return camel_folder_summary_header_save_to_db (s, error);

	camel_db_begin_transaction (cdb, NULL);

	ret = save_message_infos_to_db (s, FALSE, error);
	if (ret != 0) {
		camel_db_abort_transaction (cdb, NULL);
		/* Failed, so lets reset the flag */
		s->flags |= CAMEL_SUMMARY_DIRTY;
		return -1;
	}

	/* XXX So... if an error is set, how do we even reach this point
	 *     given the above error check?  Oye vey this logic is nasty. */
	if (error != NULL && *error != NULL &&
		strstr ((*error)->message, "26 columns but 28 values") != NULL) {
		const gchar *full_name;

		/* This is an error is previous migration. Let remigrate this folder alone. */
		camel_db_abort_transaction (cdb, NULL);
		full_name = camel_folder_get_full_name (s->folder);
		camel_db_reset_folder_version (cdb, full_name, 0, NULL);
		g_warning ("Fixing up a broken summary migration on %s\n", full_name);
		/* Begin everything again. */
		camel_db_begin_transaction (cdb, NULL);

		ret = save_message_infos_to_db (s, FALSE, error);
		if (ret != 0) {
			camel_db_abort_transaction (cdb, NULL);
			/* Failed, so lets reset the flag */
			s->flags |= CAMEL_SUMMARY_DIRTY;
			return -1;
		}
	}

	camel_db_end_transaction (cdb, NULL);

	record = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->summary_header_to_db (s, error);
	if (!record) {
		s->flags |= CAMEL_SUMMARY_DIRTY;
		return -1;
	}

	camel_db_begin_transaction (cdb, NULL);
	ret = camel_db_write_folder_info_record (cdb, record, error);
	g_free (record->folder_name);
	g_free (record->bdata);
	g_free (record);

	if (ret != 0) {
		camel_db_abort_transaction (cdb, NULL);
		s->flags |= CAMEL_SUMMARY_DIRTY;
		return -1;
	}

	camel_db_end_transaction (cdb, NULL);

	return ret;
}

/**
 * camel_folder_summary_header_save_to_db:
 *
 * Since: 2.24
 **/
gint
camel_folder_summary_header_save_to_db (CamelFolderSummary *s,
                                        GError **error)
{
	CamelStore *parent_store;
	CamelFIRecord *record;
	CamelDB *cdb;
	gint ret;

	parent_store = camel_folder_get_parent_store (s->folder);
	cdb = parent_store->cdb_w;

	d(printf ("\ncamel_folder_summary_header_save_to_db called \n"));

	record = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->summary_header_to_db (s, error);
	if (!record) {
		return -1;
	}

	camel_db_begin_transaction (cdb, NULL);
	ret = camel_db_write_folder_info_record (cdb, record, error);
	g_free (record->bdata);
	g_free (record);

	if (ret != 0) {
		camel_db_abort_transaction (cdb, NULL);
		return -1;
	}

	camel_db_end_transaction (cdb, NULL);

	return ret;
}

/**
 * camel_folder_summary_header_load_from_db:
 *
 * Since: 2.24
 **/
gint
camel_folder_summary_header_load_from_db (CamelFolderSummary *s,
                                          CamelStore *store,
                                          const gchar *folder_name,
                                          GError **error)
{
	CamelDB *cdb;
	CamelFIRecord *record;
	gint ret = 0;

	d(printf ("\ncamel_folder_summary_load_from_db called \n"));
	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	cdb = store->cdb_r;

	record = g_new0 (CamelFIRecord, 1);
	camel_db_read_folder_info_record (cdb, folder_name, &record, error);

	if (record) {
		if (CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->summary_header_from_db (s, record) == -1)
			ret = -1;
	} else {
		ret = -1;
	}

	g_free (record->folder_name);
	g_free (record->bdata);
	g_free (record);

	return ret;
}

static gint
summary_assign_uid(CamelFolderSummary *s, CamelMessageInfo *info)
{
	const gchar *uid;
	CamelMessageInfo *mi;

	uid = camel_message_info_uid (info);

	if (uid == NULL || uid[0] == 0) {
		camel_pstring_free (info->uid);
		uid = info->uid = (gchar *)camel_pstring_add (camel_folder_summary_next_uid_string(s), TRUE);
	}

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	while ((mi = g_hash_table_lookup(s->loaded_infos, uid))) {
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

		if (mi == info)
			return 0;

		d(printf ("Trying to insert message with clashing uid (%s).  new uid re-assigned", camel_message_info_uid (info)));

		camel_pstring_free (info->uid);
		uid = info->uid = camel_pstring_add (camel_folder_summary_next_uid_string(s), TRUE);
		camel_message_info_set_flags(info, CAMEL_MESSAGE_FOLDER_FLAGGED, CAMEL_MESSAGE_FOLDER_FLAGGED);

		camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	}

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	return 1;
}

/**
 * camel_folder_summary_add:
 * @summary: a #CamelFolderSummary object
 * @info: a #CamelMessageInfo
 *
 * Adds a new @info record to the summary.  If @info->uid is %NULL,
 * then a new uid is automatically re-assigned by calling
 * #camel_folder_summary_next_uid_string.
 *
 * The @info record should have been generated by calling one of the
 * info_new_*() functions, as it will be free'd based on the summary
 * class.  And MUST NOT be allocated directly using malloc.
 **/
void
camel_folder_summary_add (CamelFolderSummary *s, CamelMessageInfo *info)
{
	if (info == NULL)
		return;

	if (summary_assign_uid(s, info) == 0)
		return;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	/* Summary always holds a ref for the loaded infos */
	/* camel_message_info_ref(info); FIXME: Check how things are loaded. */
	/* FIXME[disk-summary] SHould we ref it or redesign it later on */
	/* The uid array should have its own memory. We will unload the infos when not reqd.*/
	g_ptr_array_add (s->uids, (gpointer) camel_pstring_strdup((camel_message_info_uid(info))));
	g_hash_table_replace (CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s)->flag_cache, (gchar *)info->uid, GUINT_TO_POINTER(camel_message_info_flags(info)));

	g_hash_table_insert (s->loaded_infos, (gpointer) camel_message_info_uid (info), info);
	s->flags |= CAMEL_SUMMARY_DIRTY;

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
}

/**
 * camel_folder_summary_insert:
 *
 * Since: 2.24
 **/
void
camel_folder_summary_insert (CamelFolderSummary *s, CamelMessageInfo *info, gboolean load)
{
	if (info == NULL)
		return;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	/* Summary always holds a ref for the loaded infos */
	/* camel_message_info_ref(info); FIXME: Check how things are loaded. */
	/* FIXME[disk-summary] SHould we ref it or redesign it later on */
	/* The uid array should have its own memory. We will unload the infos when not reqd.*/
	if (!load)
		g_ptr_array_add (s->uids, (gchar *) camel_pstring_strdup(camel_message_info_uid(info)));

	g_hash_table_insert (s->loaded_infos, (gchar *) camel_message_info_uid (info), info);
	if (load) {
		g_hash_table_replace (CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s)->flag_cache, (gchar *)info->uid, GUINT_TO_POINTER(camel_message_info_flags(info)));
	}

	if (!load)
		s->flags |= CAMEL_SUMMARY_DIRTY;

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
}

void
camel_folder_summary_update_counts_by_flags (CamelFolderSummary *summary, guint32 flags, gboolean subtract)
{
	gint unread=0, deleted=0, junk=0;
	gboolean is_junk_folder = FALSE, is_trash_folder = FALSE;

	g_return_if_fail (summary != NULL);

	if (summary->folder && CAMEL_IS_VTRASH_FOLDER (summary->folder)) {
		CamelVTrashFolder *vtrash = CAMEL_VTRASH_FOLDER (summary->folder);

		is_junk_folder = vtrash && vtrash->type == CAMEL_VTRASH_FOLDER_JUNK;
		is_trash_folder = vtrash && vtrash->type == CAMEL_VTRASH_FOLDER_TRASH;
	}

	if (!(flags & CAMEL_MESSAGE_SEEN))
		unread = subtract ? -1 : 1;

	if (flags & CAMEL_MESSAGE_DELETED)
		deleted = subtract ? -1 : 1;

	if (flags & CAMEL_MESSAGE_JUNK)
		junk = subtract ? -1 : 1;

	dd(printf("%p: %d %d %d | %d %d %d \n", (gpointer) summary, unread, deleted, junk, summary->unread_count, summary->visible_count, summary->saved_count));

	if (deleted)
		summary->deleted_count += deleted;
	if (junk)
		summary->junk_count += junk;
	if (junk && !deleted)
		summary->junk_not_deleted_count += junk;
	if (!junk && !deleted)
		summary->visible_count += subtract ? -1 : 1;

	if (junk && !is_junk_folder)
		unread = 0;
	if (deleted && !is_trash_folder)
		unread = 0;

	if (unread)
		summary->unread_count += unread;

	summary->saved_count += subtract ? -1 : 1;
	camel_folder_summary_touch (summary);

	dd(printf("%p: %d %d %d | %d %d %d\n", (gpointer) summary, unread, deleted, junk, summary->unread_count, summary->visible_count, summary->saved_count));
}

static void
update_summary (CamelFolderSummary *summary, CamelMessageInfoBase *info)
{
	g_return_if_fail (summary != NULL);
	g_return_if_fail (info != NULL);

	camel_folder_summary_update_counts_by_flags (summary, info->flags, FALSE);
	info->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
	info->dirty = TRUE;
}

/**
 * camel_folder_summary_add_from_header:
 * @summary: a #CamelFolderSummary object
 * @headers: rfc822 headers
 *
 * Build a new info record based on a set of headers, and add it to
 * the summary.
 *
 * Note that this function should not be used if build_content_info
 * has been specified for this summary.
 *
 * Returns: the newly added record
 **/
CamelMessageInfo *
camel_folder_summary_add_from_header (CamelFolderSummary *summary,
                                      struct _camel_header_raw *h)
{
	CamelMessageInfo *info;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);

	info = camel_folder_summary_info_new_from_header (summary, h);

	camel_folder_summary_add (summary, info);
	update_summary (summary, (CamelMessageInfoBase *) info);

	return info;
}

/**
 * camel_folder_summary_add_from_parser:
 * @summary: a #CamelFolderSummary object
 * @parser: a #CamelMimeParser object
 *
 * Build a new info record based on the current position of a #CamelMimeParser.
 *
 * The parser should be positioned before the start of the message to summarise.
 * This function may be used if build_contnet_info or an index has been
 * specified for the summary.
 *
 * Returns: the newly added record
 **/
CamelMessageInfo *
camel_folder_summary_add_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *info;

	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (s), NULL);
	g_return_val_if_fail (CAMEL_IS_MIME_PARSER (mp), NULL);

	info = camel_folder_summary_info_new_from_parser(s, mp);

	camel_folder_summary_add (s, info);
	update_summary (s, (CamelMessageInfoBase *) info);
	return info;
}

/**
 * camel_folder_summary_add_from_message:
 * @summary: a #CamelFolderSummary object
 * @message: a #CamelMimeMessage object
 *
 * Add a summary item from an existing message.
 *
 * Returns: the newly added record
 **/
CamelMessageInfo *
camel_folder_summary_add_from_message (CamelFolderSummary *s, CamelMimeMessage *msg)
{
	CamelMessageInfo *info = camel_folder_summary_info_new_from_message(s, msg, NULL);

	camel_folder_summary_add (s, info);
	update_summary (s, (CamelMessageInfoBase *) info);
	return info;
}

/**
 * camel_folder_summary_info_new_from_header:
 * @summary: a #CamelFolderSummary object
 * @headers: rfc822 headers
 *
 * Create a new info record from a header.
 *
 * Returns: the newly allocated record which must be freed with
 * #camel_message_info_free
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
 * Returns: the newly allocated record which must be freed with
 * #camel_message_info_free
 **/
CamelMessageInfo *
camel_folder_summary_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *info = NULL;
	gchar *buffer;
	gsize len;
	struct _CamelFolderSummaryPrivate *p = CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s);
	goffset start;
	CamelIndexName *name = NULL;

	/* should this check the parser is in the right state, or assume it is?? */

	start = camel_mime_parser_tell(mp);
	if (camel_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_EOF) {
		info = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->message_info_new_from_parser (s, mp);

		camel_mime_parser_unstep(mp);

		/* assign a unique uid, this is slightly 'wrong' as we do not really
		 * know if we are going to store this in the summary, but no matter */
		if (p->index)
			summary_assign_uid(s, info);

		camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_FILTER_LOCK);

		if (p->index) {
			if (p->filter_index == NULL)
				p->filter_index = camel_mime_filter_index_new (p->index);
			camel_index_delete_name(p->index, camel_message_info_uid(info));
			name = camel_index_add_name(p->index, camel_message_info_uid(info));
			camel_mime_filter_index_set_name(CAMEL_MIME_FILTER_INDEX (p->filter_index), name);
		}

		/* always scan the content info, even if we dont save it */
		((CamelMessageInfoBase *)info)->content = summary_build_content_info(s, info, mp);

		if (name && p->index) {
			camel_index_write_name(p->index, name);
			g_object_unref (name);
			camel_mime_filter_index_set_name (
				CAMEL_MIME_FILTER_INDEX (p->filter_index), NULL);
		}

		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_FILTER_LOCK);

		((CamelMessageInfoBase *)info)->size = camel_mime_parser_tell(mp) - start;
	}
	return info;
}

/**
 * camel_folder_summary_info_new_from_message:
 * @summary: a #CamelFodlerSummary object
 * @message: a #CamelMimeMessage object
 * @boydstructure: a bodystructure or NULL
 *
 * Create a summary item from a message.
 *
 * Returns: the newly allocated record which must be freed using
 * #camel_message_info_free
 **/
CamelMessageInfo *
camel_folder_summary_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg, const gchar *bodystructure)
{
	CamelMessageInfo *info;
	struct _CamelFolderSummaryPrivate *p = CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s);
	CamelIndexName *name = NULL;

	info = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->message_info_new_from_message (s, msg, bodystructure);

	/* assign a unique uid, this is slightly 'wrong' as we do not really
	 * know if we are going to store this in the summary, but we need it set for indexing */
	if (p->index)
		summary_assign_uid(s, info);

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_FILTER_LOCK);

	if (p->index) {
		if (p->filter_index == NULL)
			p->filter_index = camel_mime_filter_index_new (p->index);
		camel_index_delete_name(p->index, camel_message_info_uid(info));
		name = camel_index_add_name(p->index, camel_message_info_uid(info));
		camel_mime_filter_index_set_name (
			CAMEL_MIME_FILTER_INDEX (p->filter_index), name);

		if (p->filter_stream == NULL) {
			CamelStream *null = camel_stream_null_new();

			p->filter_stream = camel_stream_filter_new (null);
			g_object_unref (null);
		}
	}

	((CamelMessageInfoBase *)info)->content = summary_build_content_info_message(s, info, (CamelMimePart *)msg);

	if (name) {
		camel_index_write_name(p->index, name);
		g_object_unref (name);
		camel_mime_filter_index_set_name (
			CAMEL_MIME_FILTER_INDEX (p->filter_index), NULL);
	}

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_FILTER_LOCK);

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
camel_folder_summary_content_info_free(CamelFolderSummary *s, CamelMessageContentInfo *ci)
{
	CamelMessageContentInfo *pw, *pn;

	pw = ci->childs;
	CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->content_info_free (s, ci);
	while (pw) {
		pn = pw->next;
		camel_folder_summary_content_info_free(s, pw);
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
camel_folder_summary_touch(CamelFolderSummary *s)
{
	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	s->flags |= CAMEL_SUMMARY_DIRTY;
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
}

/**
 * camel_folder_summary_clear:
 * @summary: a #CamelFolderSummary object
 *
 * Empty the summary contents.
 **/
void
camel_folder_summary_clear(CamelFolderSummary *s)
{
	d(printf ("\ncamel_folder_summary_clearcalled \n"));
	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	if (camel_folder_summary_count(s) == 0) {
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
		return;
	}

	g_ptr_array_foreach (s->uids, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (s->uids, TRUE);
	s->uids = g_ptr_array_new ();
	s->saved_count = 0;
	s->unread_count = 0;
	s->deleted_count = 0;
	s->junk_count = 0;
	s->junk_not_deleted_count = 0;
	s->visible_count = 0;

	g_hash_table_destroy(s->loaded_infos);
	s->loaded_infos = g_hash_table_new(g_str_hash, g_str_equal);

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
}

/**
 * camel_folder_summary_clear_db:
 *
 * Since: 2.24
 **/
void
camel_folder_summary_clear_db (CamelFolderSummary *s)
{
	CamelStore *parent_store;
	CamelDB *cdb;
	const gchar *folder_name;

	/* FIXME: This is non-sense. Neither an exception is passed,
	nor a value returned. How is the caller supposed to know,
	whether the operation is succesful */

	d(printf ("\ncamel_folder_summary_load_from_db called \n"));
	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	folder_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);
	cdb = parent_store->cdb_w;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	if (camel_folder_summary_count(s) == 0) {
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
		return;
	}

	g_ptr_array_foreach (s->uids, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (s->uids, TRUE);
	s->uids = g_ptr_array_new ();
	s->saved_count = 0;
	s->unread_count = 0;
	s->deleted_count = 0;
	s->junk_count = 0;
	s->junk_not_deleted_count = 0;
	s->visible_count = 0;

	g_hash_table_destroy(s->loaded_infos);
	s->loaded_infos = g_hash_table_new(g_str_hash, g_str_equal);

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	camel_db_clear_folder_summary (cdb, folder_name, NULL);
}

/* This function returns 0 on success. So the caller should not bother,
deleting the uid from db when the return value is non-zero */
static gint
summary_remove_uid (CamelFolderSummary *s, const gchar *uid)
{
	gint i;

	d(printf ("\nsummary_remove_uid called \n"));

	/* This could be slower, but no otherway really. FIXME: Callers have to effective and shouldn't call it recursively. */
	for (i=0; i<s->uids->len; i++) {
		if (strcmp(s->uids->pdata[i], uid) == 0) {
			/* FIXME: Does using fast remove affect anything ? */
			g_ptr_array_remove_index(s->uids, i);
			camel_pstring_free (uid);
			return 0;
		}

	}

	return -1;
}

/**
 * camel_folder_summary_remove:
 * @summary: a #CamelFolderSummary object
 * @info: a #CamelMessageInfo
 *
 * Remove a specific @info record from the summary.
 **/
void
camel_folder_summary_remove (CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelStore *parent_store;
	const gchar *full_name;
	gboolean found;
	gint ret;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	found = g_hash_table_lookup (s->loaded_infos, camel_message_info_uid (info)) != NULL;
	g_hash_table_remove (s->loaded_infos, camel_message_info_uid(info));
	ret = summary_remove_uid (s, camel_message_info_uid(info));

	s->flags |= CAMEL_SUMMARY_DIRTY;
	s->meta_summary->msg_expunged = TRUE;
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	full_name = camel_folder_get_full_name (s->folder);
	parent_store = camel_folder_get_parent_store (s->folder);

	if (!ret && camel_db_delete_uid (parent_store->cdb_w, full_name, camel_message_info_uid(info), NULL) != 0)
		return;

	if (found)
		camel_message_info_free (info);
}

/**
 * camel_folder_summary_remove_uid:
 * @summary: a #CamelFolderSummary object
 * @uid: a uid
 *
 * Remove a specific info record from the summary, by @uid.
 **/
void
camel_folder_summary_remove_uid(CamelFolderSummary *s, const gchar *uid)
{
	CamelMessageInfo *oldinfo;
	gchar *olduid;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
	if (g_hash_table_lookup_extended(s->loaded_infos, uid, (gpointer)&olduid, (gpointer)&oldinfo)) {
		/* make sure it doesn't vanish while we're removing it */
		camel_message_info_ref (oldinfo);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
		camel_folder_summary_remove(s, oldinfo);
		camel_message_info_free(oldinfo);
	} else {
		CamelStore *parent_store;
		const gchar *full_name;
		gchar *tmpid = g_strdup (uid);
		gint ret;
		/* Info isn't loaded into the memory. We must just remove the UID*/
		ret = summary_remove_uid (s, uid);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

		full_name = camel_folder_get_full_name (s->folder);
		parent_store = camel_folder_get_parent_store (s->folder);
		if (!ret && camel_db_delete_uid (parent_store->cdb_w, full_name, tmpid, NULL) != 0) {
			g_free(tmpid);
			return;
		}
		g_free (tmpid);
	}
}

/* _fast doesn't deal with db and leaves it to the caller. */

/**
 * camel_folder_summary_remove_uid_fast:
 *
 * Since: 2.24
 **/
void
camel_folder_summary_remove_uid_fast (CamelFolderSummary *s, const gchar *uid)
{
		CamelMessageInfo *oldinfo;
		gchar *olduid;

		camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
		camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
		if (g_hash_table_lookup_extended(s->loaded_infos, uid, (gpointer)&olduid, (gpointer)&oldinfo)) {
				/* make sure it doesn't vanish while we're removing it */
				camel_message_info_ref (oldinfo);
				camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
				g_hash_table_remove (s->loaded_infos, olduid);
				summary_remove_uid (s, olduid);
				s->flags |= CAMEL_SUMMARY_DIRTY;
				s->meta_summary->msg_expunged = TRUE;
				camel_message_info_free(oldinfo);
				camel_message_info_free(oldinfo);
				camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
		} else {
				gchar *tmpid = g_strdup (uid);
				/* Info isn't loaded into the memory. We must just remove the UID*/
				summary_remove_uid (s, uid);
				camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
				camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
				g_free (tmpid);
		}
}

/**
 * camel_folder_summary_remove_index_fast:
 *
 * Since: 2.24
 **/
void
camel_folder_summary_remove_index_fast (CamelFolderSummary *s, gint index)
{
	const gchar *uid = s->uids->pdata[index];
        CamelMessageInfo *oldinfo;
        gchar *olduid;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);

	if (g_hash_table_lookup_extended(s->loaded_infos, uid, (gpointer)&olduid, (gpointer)&oldinfo)) {
		/* make sure it doesn't vanish while we're removing it */
		g_hash_table_remove (s->loaded_infos, uid);
		camel_pstring_free (uid);
		g_ptr_array_remove_index(s->uids, index);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
		camel_message_info_free(oldinfo);
	} else {
		/* Info isn't loaded into the memory. We must just remove the UID*/
		g_ptr_array_remove_index(s->uids, index);
		camel_pstring_free (uid);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_REF_LOCK);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	}
}

/**
 * camel_folder_summary_remove_index:
 * @summary: a #CamelFolderSummary object
 * @index: record index
 *
 * Remove a specific info record from the summary, by index.
 **/
void
camel_folder_summary_remove_index(CamelFolderSummary *s, gint index)
{
	const gchar *uid = s->uids->pdata[index];

	camel_folder_summary_remove_uid (s, uid);
}

/**
 * camel_folder_summary_remove_range:
 * @summary: a #CamelFolderSummary object
 * @start: initial index
 * @end: last index to remove
 *
 * Removes an indexed range of info records.
 **/
void
camel_folder_summary_remove_range (CamelFolderSummary *s, gint start, gint end)
{
	d(g_print ("\ncamel_folder_summary_remove_range called \n"));
	if (end < start)
		return;

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	if (start < s->uids->len) {

		gint i;
		CamelDB *cdb;
		CamelStore *parent_store;
		const gchar *folder_name;
		GSList *uids = NULL;

		end = MIN(end+1, s->uids->len);

		for (i = start; i < end; i++) {
			const gchar *uid = s->uids->pdata[i];
			gpointer olduid, oldinfo;

			/* the uid will be freed below and will not be used because of changing size of the s->uids array */
			uids = g_slist_prepend (uids, (gpointer) uid);

			if (g_hash_table_lookup_extended (s->loaded_infos, uid, &olduid, &oldinfo)) {
				camel_message_info_free (oldinfo);
				g_hash_table_remove (s->loaded_infos, uid);
			}
		}

		folder_name = camel_folder_get_full_name (s->folder);
		parent_store = camel_folder_get_parent_store (s->folder);
		cdb = parent_store->cdb_w;

		/* FIXME[disk-summary] lifecycle of infos should be checked.
		 * Add should add to db and del should del to db. Sync only
		 * the changes at interval and remove those full sync on
		 * folder switch */
		camel_db_delete_uids (cdb, folder_name, uids, NULL);

		g_slist_foreach (uids, (GFunc) camel_pstring_free, NULL);
		g_slist_free (uids);

		memmove(s->uids->pdata+start, s->uids->pdata+end, (s->uids->len-end)*sizeof(s->uids->pdata[0]));
		g_ptr_array_set_size(s->uids, s->uids->len - (end - start));

		s->flags |= CAMEL_SUMMARY_DIRTY;

		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	} else {
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	}
}

/* should be sorted, for binary search */
/* This is a tokenisation mechanism for strings written to the
   summary - to save space.
   This list can have at most 31 words. */
static const gchar * tokens[] = {
	"7bit",
	"8bit",
	"alternative",
	"application",
	"base64",
	"boundary",
	"charset",
	"filename",
	"html",
	"image",
	"iso-8859-1",
	"iso-8859-8",
	"message",
	"mixed",
	"multipart",
	"name",
	"octet-stream",
	"parallel",
	"plain",
	"postscript",
	"quoted-printable",
	"related",
	"rfc822",
	"text",
	"us-ascii",		/* 25 words */
};

/* baiscally ...
    0 = null
    1-len(tokens) == tokens[id-1]
    >=32 string, length = n-32
*/

#ifdef USE_BSEARCH
static gint
token_search_cmp(gchar *key, gchar **index)
{
	d(printf("comparing '%s' to '%s'\n", key, *index));
	return strcmp(key, *index);
}
#endif

/**
 * camel_folder_summary_encode_token:
 * @out: output FILE pointer
 * @str: string token to encode
 *
 * Encode a string value, but use tokenisation and compression
 * to reduce the size taken for common mailer words.  This
 * can still be used to encode normal strings as well.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_folder_summary_encode_token(FILE *out, const gchar *str)
{
	io(printf("Encoding token: '%s'\n", str));

	if (str == NULL) {
		return camel_file_util_encode_uint32(out, 0);
	} else {
		gint len = strlen(str);
		gint i, token=-1;

		if (len <= 16) {
			gchar lower[32];
			const gchar **match;

			for (i=0;i<len;i++)
				lower[i] = tolower(str[i]);
			lower[i] = 0;
#ifdef USE_BSEARCH
			match = bsearch(lower, tokens, G_N_ELEMENTS (tokens), sizeof(gchar *), (gint (*)(gconstpointer , gconstpointer ))token_search_cmp);
			if (match)
				token = match-tokens;
#else
			for (i = 0; i < G_N_ELEMENTS (tokens); i++) {
				if (!strcmp(tokens[i], lower)) {
					token = i;
					break;
				}
			}
#endif
		}
		if (token != -1) {
			return camel_file_util_encode_uint32(out, token+1);
		} else {
			if (camel_file_util_encode_uint32(out, len+32) == -1)
				return -1;
			if (fwrite(str, len, 1, out) != 1)
				return -1;
		}
	}
	return 0;
}

/**
 * camel_folder_summary_decode_token:
 * @in: input FILE pointer
 * @str: string pointer to hold the decoded result
 *
 * Decode a token value.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_folder_summary_decode_token(FILE *in, gchar **str)
{
	gchar *ret;
	guint32 len;

	io(printf("Decode token ...\n"));

	if (camel_file_util_decode_uint32(in, &len) == -1) {
		io(printf ("Could not decode token from file"));
		*str = NULL;
		return -1;
	}

	if (len<32) {
		if (len <= 0) {
			ret = NULL;
		} else if (len<= G_N_ELEMENTS (tokens)) {
			ret = g_strdup(tokens[len-1]);
		} else {
			io(printf ("Invalid token encountered: %d", len));
			*str = NULL;
			return -1;
		}
	} else if (len > 10240) {
		io(printf ("Got broken string header length: %d bytes", len));
		*str = NULL;
		return -1;
	} else {
		len -= 32;
		ret = g_malloc(len+1);
		if (len > 0 && fread(ret, len, 1, in) != 1) {
			g_free(ret);
			*str = NULL;
			return -1;
		}
		ret[len]=0;
	}

	io(printf("Token = '%s'\n", ret));

	*str = ret;
	return 0;
}

static struct _node *
my_list_append(struct _node **list, struct _node *n)
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
my_list_size(struct _node **list)
{
	gint len = 0;
	struct _node *ln = (struct _node *)list;
	while (ln->next) {
		ln = ln->next;
		len++;
	}
	return len;
}

static gint
summary_header_load(CamelFolderSummary *s, FILE *in)
{
	if (!s->summary_path)
		return -1;

	fseek(in, 0, SEEK_SET);

	io(printf("Loading header\n"));

	if (camel_file_util_decode_fixed_int32(in, (gint32 *) &s->version) == -1)
		return -1;

	/* Legacy version check, before version 12 we have no upgrade knowledge */
	if ((s->version > 0xff) && (s->version & 0xff) < 12) {
		io(printf ("Summary header version mismatch"));
		errno = EINVAL;
		return -1;
	}

	if (!(s->version < 0x100 && s->version >= 13)) {
		io(printf("Loading legacy summary\n"));
	} else {
		io(printf("loading new-format summary\n"));
	}

	/* legacy version */
	if (camel_file_util_decode_fixed_int32(in, (gint32 *) &s->flags) == -1
	    || camel_file_util_decode_fixed_int32(in, (gint32 *) &s->nextuid) == -1
	    || camel_file_util_decode_time_t(in, &s->time) == -1
	    || camel_file_util_decode_fixed_int32(in, (gint32 *) &s->saved_count) == -1) {
		return -1;
	}

	/* version 13 */
	if (s->version < 0x100 && s->version >= 13
	    && (camel_file_util_decode_fixed_int32(in, (gint32 *) &s->unread_count) == -1
		|| camel_file_util_decode_fixed_int32(in, (gint32 *) &s->deleted_count) == -1
		|| camel_file_util_decode_fixed_int32(in, (gint32 *) &s->junk_count) == -1)) {
		return -1;
	}

	return 0;
}

/* are these even useful for anything??? */
static CamelMessageInfo *
message_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageInfo *mi = NULL;
	gint state;

	state = camel_mime_parser_state(mp);
	switch (state) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		mi = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->message_info_new_from_header (s, camel_mime_parser_headers_raw (mp));
		break;
	default:
		g_error("Invalid parser state");
	}

	return mi;
}

static CamelMessageContentInfo *
content_info_new_from_parser(CamelFolderSummary *s, CamelMimeParser *mp)
{
	CamelMessageContentInfo *ci = NULL;

	switch (camel_mime_parser_state(mp)) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		ci = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->content_info_new_from_header (s, camel_mime_parser_headers_raw (mp));
		if (ci) {
			ci->type = camel_mime_parser_content_type(mp);
			camel_content_type_ref(ci->type);
		}
		break;
	default:
		g_error("Invalid parser state");
	}

	return ci;
}

static CamelMessageInfo *
message_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg, const gchar *bodystructure)
{
	CamelMessageInfo *mi;

	mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_header(s, ((CamelMimePart *)msg)->headers);
	((CamelMessageInfoBase *)mi)->bodystructure = g_strdup (bodystructure);

	return mi;
}

static CamelMessageContentInfo *
content_info_new_from_message(CamelFolderSummary *s, CamelMimePart *mp)
{
	CamelMessageContentInfo *ci;

	ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new_from_header(s, mp->headers);

	return ci;
}

static gchar *
summary_format_address(struct _camel_header_raw *h, const gchar *name, const gchar *charset)
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
summary_format_string (struct _camel_header_raw *h, const gchar *name, const gchar *charset)
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
camel_folder_summary_content_info_new(CamelFolderSummary *s)
{
	CamelFolderSummaryClass *class;
	CamelMessageContentInfo *ci;

	class = CAMEL_FOLDER_SUMMARY_GET_CLASS (s);

	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_ALLOC_LOCK);
	ci = g_slice_alloc0 (class->content_info_size);
	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_ALLOC_LOCK);

	return ci;
}

static CamelMessageInfo *
message_info_new_from_header(CamelFolderSummary *s, struct _camel_header_raw *h)
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

	mi = (CamelMessageInfoBase *)camel_message_info_new(s);

	if ((content = camel_header_raw_find(&h, "Content-Type", NULL))
	     && (ct = camel_content_type_decode(content))
	     && (charset = camel_content_type_param(ct, "charset"))
	     && (g_ascii_strcasecmp(charset, "us-ascii") == 0))
		charset = NULL;

	charset = charset ? camel_iconv_charset_name (charset) : NULL;

	subject = summary_format_string(h, "subject", charset);
	from = summary_format_address(h, "from", charset);
	to = summary_format_address(h, "to", charset);
	cc = summary_format_address(h, "cc", charset);
	mlist = camel_header_raw_check_mailing_list(&h);

	if (ct)
		camel_content_type_unref(ct);

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

	received = camel_header_raw_find(&h, "received", NULL);
	if (received)
		received = strrchr(received, ';');
	if (received)
		mi->date_received = camel_header_decode_date(received + 1, NULL);
	else
		mi->date_received = 0;

	msgid = camel_header_msgid_decode(camel_header_raw_find(&h, "message-id", NULL));
	if (msgid) {
		GChecksum *checksum;

		checksum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (checksum, (guchar *) msgid, -1);
		g_checksum_get_digest (checksum, digest, &length);
		g_checksum_free (checksum);

		memcpy(mi->message_id.id.hash, digest, sizeof(mi->message_id.id.hash));
		g_free(msgid);
	}

	/* decode our references and in-reply-to headers */
	refs = camel_header_references_decode (camel_header_raw_find (&h, "references", NULL));
	irt = camel_header_references_inreplyto_decode (camel_header_raw_find (&h, "in-reply-to", NULL));
	if (refs || irt) {
		if (irt) {
			/* The References field is populated from the "References" and/or "In-Reply-To"
			   headers. If both headers exist, take the first thing in the In-Reply-To header
			   that looks like a Message-ID, and append it to the References header. */

			if (refs)
				irt->next = refs;

			refs = irt;
		}

		count = camel_header_references_list_size(&refs);
		mi->references = g_malloc(sizeof(*mi->references) + ((count-1) * sizeof(mi->references->references[0])));
		count = 0;
		scan = refs;
		while (scan) {
			GChecksum *checksum;

			checksum = g_checksum_new (G_CHECKSUM_MD5);
			g_checksum_update (checksum, (guchar *) scan->id, -1);
			g_checksum_get_digest (checksum, digest, &length);
			g_checksum_free (checksum);

			memcpy(mi->references->references[count].id.hash, digest, sizeof(mi->message_id.id.hash));
			count++;
			scan = scan->next;
		}
		mi->references->size = count;
		camel_header_references_list_clear(&refs);
	}

	return (CamelMessageInfo *)mi;
}

static CamelMessageInfo *
message_info_migrate (CamelFolderSummary *s, FILE *in)
{
	CamelMessageInfoBase *mi;
	guint32 count;
	gint i;
	gchar *subject, *from, *to, *cc, *mlist, *uid;

	mi = (CamelMessageInfoBase *)camel_message_info_new(s);

	io(printf("Loading message info\n"));

	camel_file_util_decode_string(in, &uid);
	camel_file_util_decode_uint32(in, &mi->flags);
	camel_file_util_decode_uint32(in, &mi->size);
	camel_file_util_decode_time_t(in, &mi->date_sent);
	camel_file_util_decode_time_t(in, &mi->date_received);
	camel_file_util_decode_string(in, &subject);
	camel_file_util_decode_string(in, &from);
	camel_file_util_decode_string(in, &to);
	camel_file_util_decode_string(in, &cc);
	camel_file_util_decode_string(in, &mlist);

	mi->uid = camel_pstring_add (uid, TRUE);
	mi->subject = camel_pstring_add (subject, TRUE);
	mi->from = camel_pstring_add (from, TRUE);
	mi->to = camel_pstring_add (to, TRUE);
	mi->cc = camel_pstring_add (cc, TRUE);
	mi->mlist = camel_pstring_add (mlist, TRUE);

	mi->content = NULL;

	camel_file_util_decode_fixed_int32(in, (gint32 *) &mi->message_id.id.part.hi);
	camel_file_util_decode_fixed_int32(in, (gint32 *) &mi->message_id.id.part.lo);

	if (camel_file_util_decode_uint32(in, &count) == -1)
		goto error;

	if (count > 0) {
		mi->references = g_malloc(sizeof(*mi->references) + ((count-1) * sizeof(mi->references->references[0])));
		mi->references->size = count;
		for (i=0;i<count;i++) {
			camel_file_util_decode_fixed_int32(in, (gint32 *) &mi->references->references[i].id.part.hi);
			camel_file_util_decode_fixed_int32(in, (gint32 *) &mi->references->references[i].id.part.lo);
		}
	}

	if (camel_file_util_decode_uint32(in, &count) == -1)
		goto error;

	for (i=0;i<count;i++) {
		gchar *name;
		if (camel_file_util_decode_string(in, &name) == -1 || name == NULL)
			goto error;
		camel_flag_set(&mi->user_flags, name, TRUE);
		g_free(name);
	}

	if (camel_file_util_decode_uint32(in, &count) == -1)
		goto error;

	for (i=0;i<count;i++) {
		gchar *name, *value;
		if (camel_file_util_decode_string(in, &name) == -1 || name == NULL
		    || camel_file_util_decode_string(in, &value) == -1)
			goto error;
		camel_tag_set(&mi->user_tags, name, value);
		g_free(name);
		g_free(value);
	}

	if (!ferror(in))
		return (CamelMessageInfo *)mi;

error:
	camel_message_info_free((CamelMessageInfo *)mi);

	return NULL;
}

static void
message_info_free(CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelFolderSummaryClass *class;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;

	if (mi->uid) {
		if (s && g_hash_table_lookup (s->loaded_infos, mi->uid) == mi) {
			g_hash_table_remove (s->loaded_infos, mi->uid);
		}
		camel_pstring_free(mi->uid);
	}
	camel_pstring_free(mi->subject);
	camel_pstring_free(mi->from);
	camel_pstring_free(mi->to);
	camel_pstring_free(mi->cc);
	camel_pstring_free(mi->mlist);
	g_free (mi->bodystructure);
	g_free(mi->references);
	g_free (mi->preview);
	camel_flag_list_free(&mi->user_flags);
	camel_tag_list_free(&mi->user_tags);
	if (mi->headers)
		camel_header_param_list_free (mi->headers);

	if (s) {
		class = CAMEL_FOLDER_SUMMARY_GET_CLASS (s);
		g_slice_free1 (class->message_info_size, mi);
	} else
		g_slice_free (CamelMessageInfoBase, mi);
}

static CamelMessageContentInfo *
content_info_new_from_header(CamelFolderSummary *s, struct _camel_header_raw *h)
{
	CamelMessageContentInfo *ci;
	const gchar *charset;

	ci = camel_folder_summary_content_info_new (s);

	charset = camel_iconv_locale_charset ();
	ci->id = camel_header_msgid_decode (camel_header_raw_find (&h, "content-id", NULL));
	ci->description = camel_header_decode_string (camel_header_raw_find (&h, "content-description", NULL), charset);
	ci->encoding = camel_content_transfer_encoding_decode (camel_header_raw_find (&h, "content-transfer-encoding", NULL));
	ci->type = camel_content_type_decode(camel_header_raw_find(&h, "content-type", NULL));

	return ci;
}

static CamelMessageContentInfo *
content_info_migrate (CamelFolderSummary *s, FILE *in)
{
	CamelMessageContentInfo *ci;
	gchar *type, *subtype;
	guint32 count, i;
	CamelContentType *ct;

	io(printf("Loading content info\n"));

	ci = camel_folder_summary_content_info_new(s);

	camel_folder_summary_decode_token(in, &type);
	camel_folder_summary_decode_token(in, &subtype);
	ct = camel_content_type_new(type, subtype);
	g_free(type);		/* can this be removed? */
	g_free(subtype);

	if (camel_file_util_decode_uint32(in, &count) == -1)
		goto error;

	for (i = 0; i < count; i++) {
		gchar *name, *value;
		camel_folder_summary_decode_token(in, &name);
		camel_folder_summary_decode_token(in, &value);
		if (!(name && value))
			goto error;

		camel_content_type_set_param(ct, name, value);
		/* TODO: do this so we dont have to double alloc/free */
		g_free(name);
		g_free(value);
	}
	ci->type = ct;

	camel_folder_summary_decode_token(in, &ci->id);
	camel_folder_summary_decode_token(in, &ci->description);
	camel_folder_summary_decode_token(in, &ci->encoding);

	camel_file_util_decode_uint32(in, &ci->size);

	ci->childs = NULL;

	if (!ferror(in))
		return ci;

 error:
	camel_folder_summary_content_info_free(s, ci);
	return NULL;
}

static void
content_info_free(CamelFolderSummary *s, CamelMessageContentInfo *ci)
{
	CamelFolderSummaryClass *class;

	class = CAMEL_FOLDER_SUMMARY_GET_CLASS (s);

	camel_content_type_unref(ci->type);
	g_free(ci->id);
	g_free(ci->description);
	g_free(ci->encoding);
	g_slice_free1 (class->content_info_size, ci);
}

static gchar *
next_uid_string(CamelFolderSummary *s)
{
	return g_strdup_printf("%u", camel_folder_summary_next_uid(s));
}

/*
  OK
  Now this is where all the "smarts" happen, where the content info is built,
  and any indexing and what not is performed
*/

/* must have filter_lock before calling this function */
static CamelMessageContentInfo *
summary_build_content_info(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimeParser *mp)
{
	gint state;
	gsize len;
	gchar *buffer;
	CamelMessageContentInfo *info = NULL;
	CamelContentType *ct;
	gint enc_id = -1, chr_id = -1, html_id = -1, idx_id = -1;
	struct _CamelFolderSummaryPrivate *p = CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s);
	CamelMimeFilter *mfc;
	CamelMessageContentInfo *part;
	const gchar *calendar_header;

	d(printf("building content info\n"));

	/* start of this part */
	state = camel_mime_parser_step(mp, &buffer, &len);

	if (s->build_content)
		info = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->content_info_new_from_parser (s, mp);

	switch (state) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
		/* check content type for indexing, then read body */
		ct = camel_mime_parser_content_type(mp);
		/* update attachments flag as we go */
		if (camel_content_type_is(ct, "application", "pgp-signature")
#ifdef ENABLE_SMIME
		    || camel_content_type_is(ct, "application", "x-pkcs7-signature")
		    || camel_content_type_is(ct, "application", "pkcs7-signature")
#endif
			)
			camel_message_info_set_flags(msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);

		calendar_header = camel_mime_parser_header (mp, "Content-class", NULL);
		if (calendar_header && g_ascii_strcasecmp (calendar_header, "urn:content-classes:calendarmessage") != 0)
			calendar_header = NULL;

		if (!calendar_header)
			calendar_header = camel_mime_parser_header (mp, "X-Calendar-Attachment", NULL);

		if (calendar_header || camel_content_type_is (ct, "text", "calendar"))
			camel_message_info_set_user_flag (msginfo, "$has_cal", TRUE);

		if (p->index && camel_content_type_is(ct, "text", "*")) {
			gchar *encoding;
			const gchar *charset;

			d(printf("generating index:\n"));

			encoding = camel_content_transfer_encoding_decode(camel_mime_parser_header(mp, "content-transfer-encoding", NULL));
			if (encoding) {
				if (!g_ascii_strcasecmp(encoding, "base64")) {
					d(printf(" decoding base64\n"));
					if (p->filter_64 == NULL)
						p->filter_64 = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_BASE64_DEC);
					else
						camel_mime_filter_reset(p->filter_64);
					enc_id = camel_mime_parser_filter_add(mp, p->filter_64);
				} else if (!g_ascii_strcasecmp(encoding, "quoted-printable")) {
					d(printf(" decoding quoted-printable\n"));
					if (p->filter_qp == NULL)
						p->filter_qp = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_QP_DEC);
					else
						camel_mime_filter_reset(p->filter_qp);
					enc_id = camel_mime_parser_filter_add(mp, p->filter_qp);
				} else if (!g_ascii_strcasecmp (encoding, "x-uuencode")) {
					d(printf(" decoding x-uuencode\n"));
					if (p->filter_uu == NULL)
						p->filter_uu = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_UU_DEC);
					else
						camel_mime_filter_reset(p->filter_uu);
					enc_id = camel_mime_parser_filter_add(mp, p->filter_uu);
				} else {
					d(printf(" ignoring encoding %s\n", encoding));
				}
				g_free(encoding);
			}

			charset = camel_content_type_param(ct, "charset");
			if (charset!=NULL
			    && !(g_ascii_strcasecmp(charset, "us-ascii")==0
				 || g_ascii_strcasecmp(charset, "utf-8")==0)) {
				d(printf(" Adding conversion filter from %s to UTF-8\n", charset));
				mfc = g_hash_table_lookup(p->filter_charset, charset);
				if (mfc == NULL) {
					mfc = camel_mime_filter_charset_new (charset, "UTF-8");
					if (mfc)
						g_hash_table_insert(p->filter_charset, g_strdup(charset), mfc);
				} else {
					camel_mime_filter_reset((CamelMimeFilter *)mfc);
				}
				if (mfc) {
					chr_id = camel_mime_parser_filter_add(mp, mfc);
				} else {
					w(g_warning("Cannot convert '%s' to 'UTF-8', message index may be corrupt", charset));
				}
			}

			/* we do charset conversions before this filter, which isn't strictly correct,
			   but works in most cases */
			if (camel_content_type_is(ct, "text", "html")) {
				if (p->filter_html == NULL)
					p->filter_html = camel_mime_filter_html_new();
				else
					camel_mime_filter_reset((CamelMimeFilter *)p->filter_html);
				html_id = camel_mime_parser_filter_add(mp, (CamelMimeFilter *)p->filter_html);
			}

			/* and this filter actually does the indexing */
			idx_id = camel_mime_parser_filter_add(mp, p->filter_index);
		}
		/* and scan/index everything */
		while (camel_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_BODY_END)
			;
		/* and remove the filters */
		camel_mime_parser_filter_remove(mp, enc_id);
		camel_mime_parser_filter_remove(mp, chr_id);
		camel_mime_parser_filter_remove(mp, html_id);
		camel_mime_parser_filter_remove(mp, idx_id);
		break;
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		d(printf("Summarising multipart\n"));
		/* update attachments flag as we go */
		ct = camel_mime_parser_content_type(mp);
		if (camel_content_type_is(ct, "multipart", "mixed"))
			camel_message_info_set_flags(msginfo, CAMEL_MESSAGE_ATTACHMENTS, CAMEL_MESSAGE_ATTACHMENTS);
		if (camel_content_type_is(ct, "multipart", "signed")
		    || camel_content_type_is(ct, "multipart", "encrypted"))
			camel_message_info_set_flags(msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);

		while (camel_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_MULTIPART_END) {
			camel_mime_parser_unstep(mp);
			part = summary_build_content_info(s, msginfo, mp);
			if (part) {
				part->parent = info;
				my_list_append((struct _node **)&info->childs, (struct _node *)part);
			}
		}
		break;
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
		d(printf("Summarising message\n"));
		/* update attachments flag as we go */
		camel_message_info_set_flags(msginfo, CAMEL_MESSAGE_ATTACHMENTS, CAMEL_MESSAGE_ATTACHMENTS);

		part = summary_build_content_info(s, msginfo, mp);
		if (part) {
			part->parent = info;
			my_list_append((struct _node **)&info->childs, (struct _node *)part);
		}
		state = camel_mime_parser_step(mp, &buffer, &len);
		if (state != CAMEL_MIME_PARSER_STATE_MESSAGE_END) {
			g_error("Bad parser state: Expecing MESSAGE_END or MESSAGE_EOF, got: %d", state);
			camel_mime_parser_unstep(mp);
		}
		break;
	}

	d(printf("finished building content info\n"));

	return info;
}

/* build the content-info, from a message */
/* this needs the filter lock since it uses filters to perform indexing */
static CamelMessageContentInfo *
summary_build_content_info_message(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimePart *object)
{
	CamelDataWrapper *containee;
	gint parts, i;
	struct _CamelFolderSummaryPrivate *p = CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s);
	CamelMessageContentInfo *info = NULL, *child;
	CamelContentType *ct;
	const struct _camel_header_raw *header;

	if (s->build_content)
		info = CAMEL_FOLDER_SUMMARY_GET_CLASS (s)->content_info_new_from_message (s, object);

	containee = camel_medium_get_content (CAMEL_MEDIUM(object));

	if (containee == NULL)
		return info;

	/* TODO: I find it odd that get_part and get_content do not
	   add a reference, probably need fixing for multithreading */

	/* check for attachments */
	ct = ((CamelDataWrapper *)containee)->mime_type;
	if (camel_content_type_is(ct, "multipart", "*")) {
		if (camel_content_type_is(ct, "multipart", "mixed"))
			camel_message_info_set_flags(msginfo, CAMEL_MESSAGE_ATTACHMENTS, CAMEL_MESSAGE_ATTACHMENTS);
		if (camel_content_type_is(ct, "multipart", "signed")
		    || camel_content_type_is(ct, "multipart", "encrypted"))
			camel_message_info_set_flags(msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);
	} else if (camel_content_type_is(ct, "application", "pgp-signature")
#ifdef ENABLE_SMIME
		    || camel_content_type_is(ct, "application", "x-pkcs7-signature")
		    || camel_content_type_is(ct, "application", "pkcs7-signature")
#endif
		) {
		camel_message_info_set_flags(msginfo, CAMEL_MESSAGE_SECURE, CAMEL_MESSAGE_SECURE);
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
	if (CAMEL_IS_MULTIPART(containee)) {
		parts = camel_multipart_get_number(CAMEL_MULTIPART(containee));

		for (i=0;i<parts;i++) {
			CamelMimePart *part = camel_multipart_get_part(CAMEL_MULTIPART(containee), i);
			g_assert(part);
			child = summary_build_content_info_message(s, msginfo, part);
			if (child) {
				child->parent = info;
				my_list_append((struct _node **)&info->childs, (struct _node *)child);
			}
		}
	} else if (CAMEL_IS_MIME_MESSAGE(containee)) {
		/* for messages we only look at its contents */
		child = summary_build_content_info_message(s, msginfo, (CamelMimePart *)containee);
		if (child) {
			child->parent = info;
			my_list_append((struct _node **)&info->childs, (struct _node *)child);
		}
	} else if (p->filter_stream
		   && camel_content_type_is(ct, "text", "*")) {
		gint html_id = -1, idx_id = -1;

		/* pre-attach html filter if required, otherwise just index filter */
		if (camel_content_type_is(ct, "text", "html")) {
			if (p->filter_html == NULL)
				p->filter_html = camel_mime_filter_html_new();
			else
				camel_mime_filter_reset((CamelMimeFilter *)p->filter_html);
			html_id = camel_stream_filter_add (
				CAMEL_STREAM_FILTER (p->filter_stream),
				(CamelMimeFilter *)p->filter_html);
		}
		idx_id = camel_stream_filter_add (
			CAMEL_STREAM_FILTER (p->filter_stream),
			p->filter_index);

		camel_data_wrapper_decode_to_stream (
			containee, p->filter_stream, NULL);
		camel_stream_flush (p->filter_stream, NULL);

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
camel_flag_get(CamelFlag **list, const gchar *name)
{
	CamelFlag *flag;
	flag = *list;
	while (flag) {
		if (!strcmp(flag->name, name))
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
camel_flag_set(CamelFlag **list, const gchar *name, gboolean value)
{
	CamelFlag *flag, *tmp;

	if (!name)
		return TRUE;

	/* this 'trick' works because flag->next is the first element */
	flag = (CamelFlag *)list;
	while (flag->next) {
		tmp = flag->next;
		if (!strcmp(flag->next->name, name)) {
			if (!value) {
				flag->next = tmp->next;
				g_free(tmp);
			}
			return !value;
		}
		flag = tmp;
	}

	if (value) {
		tmp = g_malloc(sizeof(*tmp) + strlen(name));
		strcpy(tmp->name, name);
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
camel_flag_list_size(CamelFlag **list)
{
	gint count=0;
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
camel_flag_list_free(CamelFlag **list)
{
	CamelFlag *flag, *tmp;
	flag = *list;
	while (flag) {
		tmp = flag->next;
		g_free(flag);
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
camel_flag_list_copy(CamelFlag **to, CamelFlag **from)
{
	CamelFlag *flag, *tmp;
	gint changed = FALSE;

	if (*to == NULL && from == NULL)
		return FALSE;

	/* Remove any now-missing flags */
	flag = (CamelFlag *)to;
	while (flag->next) {
		tmp = flag->next;
		if (!camel_flag_get(from, tmp->name)) {
			flag->next = tmp->next;
			g_free(tmp);
			changed = TRUE;
		} else {
			flag = tmp;
		}
	}

	/* Add any new flags */
	flag = *from;
	while (flag) {
		changed |= camel_flag_set(to, flag->name, TRUE);
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
camel_tag_get(CamelTag **list, const gchar *name)
{
	CamelTag *tag;

	tag = *list;
	while (tag) {
		if (!strcmp(tag->name, name))
			return (const gchar *)tag->value;
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
camel_tag_set(CamelTag **list, const gchar *name, const gchar *value)
{
	CamelTag *tag, *tmp;

	/* this 'trick' works because tag->next is the first element */
	tag = (CamelTag *)list;
	while (tag->next) {
		tmp = tag->next;
		if (!strcmp(tmp->name, name)) {
			if (value == NULL) { /* clear it? */
				tag->next = tmp->next;
				g_free(tmp->value);
				g_free(tmp);
				return TRUE;
			} else if (strcmp(tmp->value, value)) { /* has it changed? */
				g_free(tmp->value);
				tmp->value = g_strdup(value);
				return TRUE;
			}
			return FALSE;
		}
		tag = tmp;
	}

	if (value) {
		tmp = g_malloc(sizeof(*tmp)+strlen(name));
		strcpy(tmp->name, name);
		tmp->value = g_strdup(value);
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
camel_tag_list_size(CamelTag **list)
{
	gint count=0;
	CamelTag *tag;

	tag = *list;
	while (tag) {
		count++;
		tag = tag->next;
	}
	return count;
}

static void
rem_tag(gchar *key, gchar *value, CamelTag **to)
{
	camel_tag_set(to, key, NULL);
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
camel_tag_list_copy(CamelTag **to, CamelTag **from)
{
	gint changed = FALSE;
	CamelTag *tag;
	GHashTable *left;

	if (*to == NULL && from == NULL)
		return FALSE;

	left = g_hash_table_new(g_str_hash, g_str_equal);
	tag = *to;
	while (tag) {
		g_hash_table_insert(left, tag->name, tag);
		tag = tag->next;
	}

	tag = *from;
	while (tag) {
		changed |= camel_tag_set(to, tag->name, tag->value);
		g_hash_table_remove(left, tag->name);
		tag = tag->next;
	}

	if (g_hash_table_size(left)>0) {
		g_hash_table_foreach(left, (GHFunc)rem_tag, to);
		changed = TRUE;
	}
	g_hash_table_destroy(left);

	return changed;
}

/**
 * camel_tag_list_free:
 * @list: the address of a #CamelTag list
 *
 * Free the tag list @list.
 **/
void
camel_tag_list_free(CamelTag **list)
{
	CamelTag *tag, *tmp;
	tag = *list;
	while (tag) {
		tmp = tag->next;
		g_free(tag->value);
		g_free(tag);
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
guint32
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
camel_system_flag_get (guint32 flags, const gchar *name)
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
camel_message_info_new (CamelFolderSummary *s)
{
	CamelFolderSummaryClass *class;
	CamelMessageInfo *info;

	if (s) {
		camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_ALLOC_LOCK);
		class = CAMEL_FOLDER_SUMMARY_GET_CLASS (s);
		info = g_slice_alloc0 (class->message_info_size);
		camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_ALLOC_LOCK);
	} else {
		info = g_slice_alloc0 (sizeof(CamelMessageInfoBase));
	}

	info->refcount = 1;
	info->summary = s;

	/* We assume that mi is always dirty unless freshly read or just saved*/
	((CamelMessageInfoBase *)info)->dirty = TRUE;

	return info;
}

/**
 * camel_message_info_ref:
 * @info: a #CamelMessageInfo
 *
 * Reference an info.
 **/
void
camel_message_info_ref(gpointer o)
{
	CamelMessageInfo *mi = o;

	if (mi->summary) {
		camel_folder_summary_lock (mi->summary, CAMEL_FOLDER_SUMMARY_REF_LOCK);
		g_assert(mi->refcount >= 1);
		mi->refcount++;
		camel_folder_summary_unlock (mi->summary, CAMEL_FOLDER_SUMMARY_REF_LOCK);
	} else {
		GLOBAL_INFO_LOCK(info);
		g_assert(mi->refcount >= 1);
		mi->refcount++;
		GLOBAL_INFO_UNLOCK(info);
	}
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
 * camel_message_info_free:
 * @info: a #CamelMessageInfo
 *
 * Unref's and potentially frees a #CamelMessageInfo and its contents.
 **/
void
camel_message_info_free(gpointer o)
{
	CamelMessageInfo *mi = o;

	g_return_if_fail(mi != NULL);

	if (mi->summary) {
		camel_folder_summary_lock (mi->summary, CAMEL_FOLDER_SUMMARY_REF_LOCK);

		if (mi->refcount >= 1)
			mi->refcount--;
		if (mi->refcount > 0) {
			camel_folder_summary_unlock (mi->summary, CAMEL_FOLDER_SUMMARY_REF_LOCK);
			return;
		}

		camel_folder_summary_unlock (mi->summary, CAMEL_FOLDER_SUMMARY_REF_LOCK);

		/* FIXME: this is kinda busted, should really be handled by message info free */
		if (mi->summary->build_content
		    && ((CamelMessageInfoBase *)mi)->content) {
			camel_folder_summary_content_info_free(mi->summary, ((CamelMessageInfoBase *)mi)->content);
		}

		CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->message_info_free (mi->summary, mi);
	} else {
		GLOBAL_INFO_LOCK(info);
		mi->refcount--;
		if (mi->refcount > 0) {
			GLOBAL_INFO_UNLOCK(info);
			return;
		}
		GLOBAL_INFO_UNLOCK(info);

		message_info_free(NULL, mi);
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
camel_message_info_clone(gconstpointer o)
{
	const CamelMessageInfo *mi = o;

	if (mi->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->message_info_clone (mi->summary, mi);
	else
		return message_info_clone(NULL, mi);
}

/**
 * camel_message_info_ptr:
 * @mi: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting pointer data.
 *
 * Returns: the pointer data
 **/
gconstpointer
camel_message_info_ptr(const CamelMessageInfo *mi, gint id)
{
	if (mi->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->info_ptr (mi, id);
	else
		return info_ptr(mi, id);
}

/**
 * camel_message_info_uint32:
 * @mi: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting 32bit gint data.
 *
 * Returns: the gint data
 **/
guint32
camel_message_info_uint32(const CamelMessageInfo *mi, gint id)
{
	if (mi->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->info_uint32 (mi, id);
	else
		return info_uint32(mi, id);
}

/**
 * camel_message_info_time:
 * @mi: a #CamelMessageInfo
 * @id: info to get
 *
 * Generic accessor method for getting time_t data.
 *
 * Returns: the time_t data
 **/
time_t
camel_message_info_time(const CamelMessageInfo *mi, gint id)
{
	if (mi->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->info_time (mi, id);
	else
		return info_time(mi, id);
}

/**
 * camel_message_info_user_flag:
 * @mi: a #CamelMessageInfo
 * @id: user flag to get
 *
 * Get the state of a user flag named @id.
 *
 * Returns: the state of the user flag
 **/
gboolean
camel_message_info_user_flag(const CamelMessageInfo *mi, const gchar *id)
{
	if (mi->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->info_user_flag (mi, id);
	else
		return info_user_flag(mi, id);
}

/**
 * camel_message_info_user_tag:
 * @mi: a #CamelMessageInfo
 * @id: user tag to get
 *
 * Get the value of a user tag named @id.
 *
 * Returns: the value of the user tag
 **/
const gchar *
camel_message_info_user_tag(const CamelMessageInfo *mi, const gchar *id)
{
	if (mi->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->info_user_tag (mi, id);
	else
		return info_user_tag(mi, id);
}

/**
 * camel_folder_summary_update_flag_cache:
 *
 * Since: 2.26
 **/
void
camel_folder_summary_update_flag_cache (CamelFolderSummary *s, const gchar *uid, guint32 flag)
{
	g_hash_table_replace (CAMEL_FOLDER_SUMMARY_GET_PRIVATE(s)->flag_cache, (gchar *) uid, GUINT_TO_POINTER(flag));
}

/**
 * camel_message_info_set_flags:
 * @mi: a #CamelMessageInfo
 * @flags: mask of flags to change
 * @set: state the flags should be changed to
 *
 * Change the state of the system flags on the #CamelMessageInfo
 *
 * Returns: %TRUE if any of the flags changed or %FALSE otherwise
 **/
gboolean
camel_message_info_set_flags(CamelMessageInfo *mi, guint32 flags, guint32 set)
{
	if (mi->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->info_set_flags (mi, flags, set);
	else
		return info_set_flags(mi, flags, set);
}

/**
 * camel_message_info_set_user_flag:
 * @mi: a #CamelMessageInfo
 * @id: name of the user flag to set
 * @state: state to set the flag to
 *
 * Set the state of a user flag on a #CamelMessageInfo.
 *
 * Returns: %TRUE if the state changed or %FALSE otherwise
 **/
gboolean
camel_message_info_set_user_flag(CamelMessageInfo *mi, const gchar *id, gboolean state)
{
	if (mi->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->info_set_user_flag (mi, id, state);
	else
		return info_set_user_flag(mi, id, state);
}

/**
 * camel_message_info_set_user_tag:
 * @mi: a #CamelMessageInfo
 * @id: name of the user tag to set
 * @val: value to set
 *
 * Set the value of a user tag on a #CamelMessageInfo.
 *
 * Returns: %TRUE if the value changed or %FALSE otherwise
 **/
gboolean
camel_message_info_set_user_tag(CamelMessageInfo *mi, const gchar *id, const gchar *val)
{
	if (mi->summary)
		return CAMEL_FOLDER_SUMMARY_GET_CLASS (mi->summary)->info_set_user_tag (mi, id, val);
	else
		return info_set_user_tag(mi, id, val);
}

void
camel_content_info_dump (CamelMessageContentInfo *ci, gint depth)
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
		printf ("%scontent-type: %s/%s\n", p, ci->type->type ? ci->type->type : "(null)",
			ci->type->subtype ? ci->type->subtype : "(null)");
	else
		printf ("%scontent-type: <unset>\n", p);
	printf ("%scontent-transfer-encoding: %s\n", p, ci->encoding ? ci->encoding : "(null)");
	printf ("%scontent-description: %s\n", p, ci->description ? ci->description : "(null)");
	printf ("%ssize: %lu\n", p, (gulong) ci->size);
	ci = ci->childs;
	while (ci) {
		camel_content_info_dump (ci, depth + 1);
		ci = ci->next;
	}
}

void
camel_message_info_dump (CamelMessageInfo *mi)
{
	if (mi == NULL) {
		printf("No message?\n");
		return;
	}

	printf("Subject: %s\n", camel_message_info_subject(mi));
	printf("To: %s\n", camel_message_info_to(mi));
	printf("Cc: %s\n", camel_message_info_cc(mi));
	printf("mailing list: %s\n", camel_message_info_mlist(mi));
	printf("From: %s\n", camel_message_info_from(mi));
	printf("UID: %s\n", camel_message_info_uid(mi));
	printf("Flags: %04x\n", camel_message_info_flags(mi));
	camel_content_info_dump(((CamelMessageInfoBase *) mi)->content, 0);
}

/**
 * camel_folder_summary_set_need_preview:
 *
 * Since: 2.28
 **/
void
camel_folder_summary_set_need_preview (CamelFolderSummary *summary, gboolean preview)
{
	CAMEL_FOLDER_SUMMARY_GET_PRIVATE(summary)->need_preview = preview;
}

/**
 * camel_folder_summary_get_need_preview:
 *
 * Since: 2.28
 **/
gboolean
camel_folder_summary_get_need_preview (CamelFolderSummary *summary)
{
	return CAMEL_FOLDER_SUMMARY_GET_PRIVATE(summary)->need_preview;
}

static gboolean
compare_strings (const gchar *str1, const gchar *str2)
{
	if (str1 && str2 && !g_ascii_strcasecmp (str1, str2))
		return TRUE;
	else if (!str1 && !str2)
		return TRUE;
	else
		return FALSE;
}

static gboolean
match_content_type (CamelContentType *info_ctype, CamelContentType *ctype)
{
	const gchar *name1, *name2;

	if (!compare_strings (info_ctype->type, ctype->type))
		return FALSE;
	if (!compare_strings (info_ctype->subtype, ctype->subtype))
		return FALSE;

	name1 = camel_content_type_param (info_ctype, "name");
	name2 = camel_content_type_param (ctype, "name");
	if (!compare_strings (name1, name2))
		return FALSE;

	return TRUE;
}

/**
 * camel_folder_summary_guess_content_info:
 * @mi: a #CamelMessageInfo
 * @ctype: a #CamelContentType
 *
 * FIXME Document me!
 *
 * Since: 2.30
 **/
const CamelMessageContentInfo *
camel_folder_summary_guess_content_info (CamelMessageInfo *mi, CamelContentType *ctype)
{
	const CamelMessageContentInfo *ci = camel_message_info_content (mi);

	while (ci) {
		const CamelMessageContentInfo *child = ci;

		do {
			if (child->type && match_content_type (child->type, ctype))
				return child;

			child = child->next;
		} while (child != NULL);

		ci = ci->childs;
	}

	return NULL;
}

/**
 * camel_folder_summary_lock:
 * @summary: a #CamelFolderSummary
 * @lock: lock type to lock
 *
 * Locks #summary's #lock. Unlock it with camel_folder_summary_unlock().
 *
 * Since: 2.32
 **/
void
camel_folder_summary_lock (CamelFolderSummary *summary,
                           CamelFolderSummaryLock lock)
{
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	switch (lock) {
		case CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK:
			g_static_rec_mutex_lock (&summary->priv->summary_lock);
			break;
		case CAMEL_FOLDER_SUMMARY_IO_LOCK:
			g_static_rec_mutex_lock (&summary->priv->io_lock);
			break;
		case CAMEL_FOLDER_SUMMARY_FILTER_LOCK:
			g_static_rec_mutex_lock (&summary->priv->filter_lock);
			break;
		case CAMEL_FOLDER_SUMMARY_ALLOC_LOCK:
			g_static_rec_mutex_lock (&summary->priv->alloc_lock);
			break;
		case CAMEL_FOLDER_SUMMARY_REF_LOCK:
			g_static_rec_mutex_lock (&summary->priv->ref_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_folder_summary_unlock:
 * @summary: a #CamelFolderSummary
 * @lock: lock type to unlock
 *
 * Unlocks #summary's #lock, previously locked with camel_folder_summary_lock().
 *
 * Since: 2.32
 **/
void
camel_folder_summary_unlock (CamelFolderSummary *summary,
                             CamelFolderSummaryLock lock)
{
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));

	switch (lock) {
		case CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK:
			g_static_rec_mutex_unlock (&summary->priv->summary_lock);
			break;
		case CAMEL_FOLDER_SUMMARY_IO_LOCK:
			g_static_rec_mutex_unlock (&summary->priv->io_lock);
			break;
		case CAMEL_FOLDER_SUMMARY_FILTER_LOCK:
			g_static_rec_mutex_unlock (&summary->priv->filter_lock);
			break;
		case CAMEL_FOLDER_SUMMARY_ALLOC_LOCK:
			g_static_rec_mutex_unlock (&summary->priv->alloc_lock);
			break;
		case CAMEL_FOLDER_SUMMARY_REF_LOCK:
			g_static_rec_mutex_unlock (&summary->priv->ref_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

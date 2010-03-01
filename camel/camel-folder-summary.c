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
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>
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
#include "camel-private.h"
#include "camel-session.h"
#include "camel-stream-filter.h"
#include "camel-stream-mem.h"
#include "camel-stream-null.h"
#include "camel-string-utils.h"
#include "camel-store.h"
#include "camel-vee-folder.h"
#include "camel-mime-part-utils.h"

/* To switch between e-memchunk and g-alloc */
#define ALWAYS_ALLOC 1
#define USE_GSLICE 1

/* Make 5 minutes as default cache drop */
#define SUMMARY_CACHE_DROP 300 
#define dd(x) if (camel_debug("sync")) x

static pthread_mutex_t info_lock = PTHREAD_MUTEX_INITIALIZER;

/* this lock is ONLY for the standalone messageinfo stuff */
#define GLOBAL_INFO_LOCK(i) pthread_mutex_lock(&info_lock)
#define GLOBAL_INFO_UNLOCK(i) pthread_mutex_unlock(&info_lock)

/* this should probably be conditional on it existing */
#define USE_BSEARCH

#define d(x)
#define io(x)			/* io debug */
#define w(x)

#if 0
extern gint strdup_count, malloc_count, free_count;
#endif

#define CAMEL_FOLDER_SUMMARY_VERSION (14)

#define _PRIVATE(o) (((CamelFolderSummary *)(o))->priv)

#define META_SUMMARY_SUFFIX_LEN 5 /* strlen("-meta") */

#define EXTRACT_FIRST_STRING(val) len=strtoul (part, &part, 10); if (*part) part++; val=g_strndup (part, len); part+=len;
#define EXTRACT_STRING(val) if (*part) part++; len=strtoul (part, &part, 10); if (*part) part++; val=g_strndup (part, len); part+=len;
#define EXTRACT_FIRST_DIGIT(val) val=strtoul (part, &part, 10);
#define EXTRACT_DIGIT(val) if (*part && *part == ' ') part++; val=strtoul (part, &part, 10);

/* trivial lists, just because ... */
struct _node {
	struct _node *next;
};

static struct _node *my_list_append(struct _node **list, struct _node *n);
static gint my_list_size(struct _node **list);

static gint summary_header_load(CamelFolderSummary *, FILE *);
static gint summary_header_save(CamelFolderSummary *, FILE *);
#if 0
static gint summary_meta_header_load(CamelFolderSummary *, FILE *);
static gint summary_meta_header_save(CamelFolderSummary *, FILE *);
#endif

static CamelMessageInfo * message_info_new_from_header(CamelFolderSummary *, struct _camel_header_raw *);
static CamelMessageInfo * message_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageInfo * message_info_new_from_message(CamelFolderSummary *s, CamelMimeMessage *msg, const gchar *bodystructure);
static CamelMessageInfo * message_info_load(CamelFolderSummary *, FILE *);
static gint		  message_info_save(CamelFolderSummary *, FILE *, CamelMessageInfo *);
static gint		  meta_message_info_save(CamelFolderSummary *s, FILE *out_meta, FILE *out, CamelMessageInfo *info);
static void		  message_info_free(CamelFolderSummary *, CamelMessageInfo *);

static CamelMessageContentInfo * content_info_new_from_header(CamelFolderSummary *, struct _camel_header_raw *);
static CamelMessageContentInfo * content_info_new_from_parser(CamelFolderSummary *, CamelMimeParser *);
static CamelMessageContentInfo * content_info_new_from_message(CamelFolderSummary *s, CamelMimePart *mp);
static CamelMessageContentInfo * content_info_load(CamelFolderSummary *, FILE *);
static gint			 content_info_save(CamelFolderSummary *, FILE *, CamelMessageContentInfo *);
static void			 content_info_free(CamelFolderSummary *, CamelMessageContentInfo *);

static gint save_message_infos_to_db (CamelFolderSummary *s, gboolean fresh_mir, CamelException *ex);
static gint camel_read_mir_callback (gpointer  ref, gint ncol, gchar ** cols, gchar ** name);

static gchar *next_uid_string(CamelFolderSummary *s);

static CamelMessageContentInfo * summary_build_content_info(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimeParser *mp);
static CamelMessageContentInfo * summary_build_content_info_message(CamelFolderSummary *s, CamelMessageInfo *msginfo, CamelMimePart *object);

static void camel_folder_summary_class_init (CamelFolderSummaryClass *klass);
static void camel_folder_summary_init       (CamelFolderSummary *obj);
static void camel_folder_summary_finalize   (CamelObject *obj);

static CamelObjectClass *camel_folder_summary_parent;
static CamelMessageInfo * message_info_from_uid (CamelFolderSummary *s, const gchar *uid);

static void
camel_folder_summary_init (CamelFolderSummary *s)
{
	struct _CamelFolderSummaryPrivate *p;

	p = _PRIVATE(s) = g_malloc0(sizeof(*p));

	p->filter_charset = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);

	s->message_info_size = sizeof(CamelMessageInfoBase);
	s->content_info_size = sizeof(CamelMessageContentInfo);
	p->flag_cache = g_hash_table_new (g_str_hash, g_str_equal);

	s->message_info_chunks = NULL;
	s->content_info_chunks = NULL;
	p->need_preview = FALSE;
	p->preview_updates = g_hash_table_new (g_str_hash, g_str_equal);

	s->version = CAMEL_FOLDER_SUMMARY_VERSION;
	s->flags = 0;
	s->time = 0;
	s->nextuid = 1;

	s->uids = g_ptr_array_new ();
	s->loaded_infos = g_hash_table_new (g_str_hash, g_str_equal);

	p->summary_lock = g_mutex_new();
	p->io_lock = g_mutex_new();
	p->filter_lock = g_mutex_new();
	p->alloc_lock = g_mutex_new();
	p->ref_lock = g_mutex_new();

	s->meta_summary = g_malloc0(sizeof(CamelFolderMetaSummary));

	/* Default is 20, any implementor having UIDs that has length
	   exceeding 20, has to override this value
	*/
	s->meta_summary->uid_len = 20;
	s->cache_load_time = 0;
	s->timeout_handle = 0;
}

static void free_o_name(gpointer key, gpointer value, gpointer data)
{
	camel_object_unref((CamelObject *)value);
	g_free(key);
}

static void
camel_folder_summary_finalize (CamelObject *obj)
{
	struct _CamelFolderSummaryPrivate *p;
	CamelFolderSummary *s = (CamelFolderSummary *)obj;

	p = _PRIVATE(obj);
	g_hash_table_destroy (p->flag_cache);
	if (s->timeout_handle)
		g_source_remove (s->timeout_handle);
	/*camel_folder_summary_clear(s);*/
	g_ptr_array_foreach (s->uids, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (s->uids, TRUE);
	g_hash_table_destroy (s->loaded_infos);

	g_hash_table_foreach(p->filter_charset, free_o_name, NULL);
	g_hash_table_destroy(p->filter_charset);

	g_free(s->summary_path);

#ifndef ALWAYS_ALLOC
	if (s->message_info_chunks)
		e_memchunk_destroy(s->message_info_chunks);
	if (s->content_info_chunks)
		e_memchunk_destroy(s->content_info_chunks);
#endif

	if (p->filter_index)
		camel_object_unref((CamelObject *)p->filter_index);
	if (p->filter_64)
		camel_object_unref((CamelObject *)p->filter_64);
	if (p->filter_qp)
		camel_object_unref((CamelObject *)p->filter_qp);
	if (p->filter_uu)
		camel_object_unref((CamelObject *)p->filter_uu);
	if (p->filter_save)
		camel_object_unref((CamelObject *)p->filter_save);
	if (p->filter_html)
		camel_object_unref((CamelObject *)p->filter_html);

	if (p->filter_stream)
		camel_object_unref((CamelObject *)p->filter_stream);
	if (p->index)
		camel_object_unref((CamelObject *)p->index);

	/* Freeing memory occupied by meta-summary-header */
	g_free(s->meta_summary->path);
	g_free(s->meta_summary);

	g_mutex_free(p->summary_lock);
	g_mutex_free(p->io_lock);
	g_mutex_free(p->filter_lock);
	g_mutex_free(p->alloc_lock);
	g_mutex_free(p->ref_lock);

	g_free(p);
}

CamelType
camel_folder_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_object_get_type (), "CamelFolderSummary",
					    sizeof (CamelFolderSummary),
					    sizeof (CamelFolderSummaryClass),
					    (CamelObjectClassInitFunc) camel_folder_summary_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_folder_summary_init,
					    (CamelObjectFinalizeFunc) camel_folder_summary_finalize);
	}

	return type;
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
	CamelFolderSummary *new = CAMEL_FOLDER_SUMMARY ( camel_object_new (camel_folder_summary_get_type ()));

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
	CAMEL_SUMMARY_LOCK(s, summary_lock);

	g_free(s->summary_path);
	s->summary_path = g_strdup(name);

	g_free(s->meta_summary->path);
	s->meta_summary->path = g_strconcat(name, "-meta", NULL);

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
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
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);

	if (p->index)
		camel_object_unref((CamelObject *)p->index);

	p->index = index;
	if (index)
		camel_object_ref((CamelObject *)index);
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

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	CAMEL_SUMMARY_LOCK(s, ref_lock);

	if (i < s->uids->len) {
		gchar *uid;
		uid = g_ptr_array_index (s->uids, i);

		/* FIXME: Get exception from caller
		and pass it on below */

		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);

		return camel_folder_summary_uid (s, uid);
	}

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

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
 **/
gchar *
camel_folder_summary_uid_from_index (CamelFolderSummary *s, gint i)
{
	gchar *uid=NULL;
	CAMEL_SUMMARY_LOCK(s, summary_lock);

	if (i<s->uids->len)
		uid = g_strdup (g_ptr_array_index(s->uids, i));

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

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
 **/
gboolean
camel_folder_summary_check_uid (CamelFolderSummary *s, const gchar *uid)
{
	gboolean ret = FALSE;
	gint i;

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	for (i=0; i<s->uids->len; i++) {
		if (strcmp(s->uids->pdata[i], uid) == 0) {
			CAMEL_SUMMARY_UNLOCK(s, summary_lock);
			return TRUE;
		}
	}

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

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

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	g_ptr_array_set_size(res, s->uids->len);
	for (i=0;i<s->uids->len;i++)
		res->pdata[i] = (gpointer) camel_pstring_strdup ((gchar *)g_ptr_array_index(s->uids, i));

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

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
 **/
GHashTable *
camel_folder_summary_get_hashtable(CamelFolderSummary *s)
{
	GHashTable *hash = g_hash_table_new (g_str_hash, g_str_equal);
	gint i;

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	for (i=0;i<s->uids->len;i++)
		g_hash_table_insert (hash, (gpointer)camel_pstring_strdup ((gchar *)g_ptr_array_index(s->uids, i)), GINT_TO_POINTER(1));

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	return hash;
}

void
camel_folder_summary_free_hashtable (GHashTable *ht)
{
	g_hash_table_foreach (ht, (GHFunc)camel_pstring_free, NULL);
	g_hash_table_destroy (ht);
}

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
	gboolean double_ref;
	gboolean add; /* or just insert to hashtable */
};

static CamelMessageInfo *
message_info_from_uid (CamelFolderSummary *s, const gchar *uid)
{
	CamelMessageInfo *info;
	gint ret;

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	info = g_hash_table_lookup (s->loaded_infos, uid);

	if (!info) {
		CamelDB *cdb;
		CamelException ex;
		gchar *folder_name;
		struct _db_pass_data data;

		d(printf ("\ncamel_folder_summary_uid called \n"));
		camel_exception_init (&ex);
		s->flags &= ~CAMEL_SUMMARY_DIRTY;

		folder_name = s->folder->full_name;
		cdb = s->folder->parent_store->cdb_r;

		CAMEL_SUMMARY_UNLOCK(s, summary_lock);

		data.summary = s;
		data.double_ref = TRUE;
		data.add = FALSE;

		ret = camel_db_read_message_info_record_with_uid (cdb, folder_name, uid, &data, camel_read_mir_callback, &ex);
		if (ret != 0) {
			camel_exception_clear (&ex);
			return NULL;
		}

		CAMEL_SUMMARY_LOCK(s, summary_lock);

		/* We would have double reffed at camel_read_mir_callback */
		info = g_hash_table_lookup (s->loaded_infos, uid);

		if (!info) {
			gchar *errmsg = g_strdup_printf ("no uid [%s] exists", uid);

			/* Makes no sense now as the exception is local as of now. FIXME: Pass exception from caller */
			camel_exception_set (&ex, CAMEL_EXCEPTION_SYSTEM, _(errmsg));
			d(g_warning ("No uid[%s] exists in %s\n", uid, folder_name));
			camel_exception_clear (&ex);
			g_free (errmsg);
		}
	}

	if (info)
		camel_message_info_ref (info);

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

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
camel_folder_summary_uid (CamelFolderSummary *s, const gchar *uid)
{
	if (!s)
		return NULL;
	return ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_from_uid(s, uid);
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

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	uid = s->nextuid++;

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

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
	CAMEL_SUMMARY_LOCK(s, summary_lock);

	s->nextuid = MAX(s->nextuid, uid);

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
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
camel_folder_summary_next_uid_string(CamelFolderSummary *s)
{
	return ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->next_uid_string(s);
}

static CamelMessageContentInfo *
perform_content_info_load_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	gint i;
	guint32 count;
	CamelMessageContentInfo *ci, *pci;
	gchar *part;

	ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_from_db (s, mir);
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
perform_content_info_load(CamelFolderSummary *s, FILE *in)
{
	gint i;
	guint32 count;
	CamelMessageContentInfo *ci, *part;

	ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_load(s, in);
	if (ci == NULL)
		return NULL;

	if (camel_file_util_decode_uint32(in, &count) == -1) {
		camel_folder_summary_content_info_free (s, ci);
		return NULL;
	}

	for (i=0;i<count;i++) {
		part = perform_content_info_load(s, in);
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

/* FIXME[disk-summary] sucks, this function returns from memory. We need to
 * have collate or something to get the modified ones from DB and merge */
GPtrArray *
camel_folder_summary_get_changed (CamelFolderSummary *s)
{
	GPtrArray *res = g_ptr_array_new();

	CAMEL_SUMMARY_LOCK (s, summary_lock);
	g_hash_table_foreach (s->loaded_infos, (GHFunc) append_changed_uids, res);
	CAMEL_SUMMARY_UNLOCK (s, summary_lock);

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

	CAMEL_SUMMARY_LOCK (s, summary_lock);
	g_hash_table_foreach (s->loaded_infos, (GHFunc) count_changed_uids, &count);
	CAMEL_SUMMARY_UNLOCK (s, summary_lock);

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
	CamelException ex;
	GSList *to_free_list = NULL, *l;

	CAMEL_DB_RELEASE_SQLITE_MEMORY;
	camel_exception_init (&ex);
	camel_folder_sync (s->folder, FALSE, &ex);
	camel_exception_clear (&ex);

	if (time(NULL) - s->cache_load_time < SUMMARY_CACHE_DROP)
		return;

	dd(printf("removing cache for  %s %d %p\n", s->folder ? s->folder->full_name : s->summary_path, g_hash_table_size (s->loaded_infos), (gpointer) s->loaded_infos));
	/* FIXME[disk-summary] hack. fix it */
	CAMEL_SUMMARY_LOCK (s, summary_lock);

	CAMEL_SUMMARY_LOCK(s, ref_lock);
	g_hash_table_foreach_remove  (s->loaded_infos, (GHRFunc) remove_item, &to_free_list);
	CAMEL_SUMMARY_UNLOCK(s, ref_lock);

	/* Deferred freeing as _free function will try to remove
	   entries from the hash_table in foreach_remove otherwise */
	for (l = to_free_list; l; l = l->next)
		camel_message_info_free (l->data);
	g_slist_free (to_free_list);

	CAMEL_SUMMARY_UNLOCK (s, summary_lock);
	dd(printf("done .. now %d\n",g_hash_table_size (s->loaded_infos)));

	s->cache_load_time = time(NULL);
}

static void remove_cache_end (CamelSession *session, CamelSessionThreadMsg *msg)
{
		struct _folder_summary_free_msg *m = (struct _folder_summary_free_msg *)msg;
		camel_object_unref (m->summary);
}

static CamelSessionThreadOps remove_timeout_ops = {
	remove_cache,
	remove_cache_end,
};

static gboolean
cfs_try_release_memory (CamelFolderSummary *s)
{
	struct _folder_summary_free_msg *m;
	CamelSession *session;

	/* If folder is freed or if the cache is nil then clean up */
	if (!s->folder || !g_hash_table_size(s->loaded_infos)) {
		s->cache_load_time = 0;
		s->timeout_handle = 0;
		return FALSE;
	}

	session = ((CamelService *)((CamelFolder *)s->folder)->parent_store)->session;

	if (time(NULL) - s->cache_load_time < SUMMARY_CACHE_DROP)
		return TRUE;

	m = camel_session_thread_msg_new(session, &remove_timeout_ops, sizeof(*m));
	camel_object_ref (s);
	m->summary = s;
	camel_session_thread_queue(session, &m->msg, 0);

	return TRUE;
}

gint
camel_folder_summary_cache_size (CamelFolderSummary *s)
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
	CamelException ex;
};

static void
msg_update_preview (const gchar *uid, gpointer value, CamelFolder *folder)
{
	CamelMessageInfoBase *info = (CamelMessageInfoBase *)camel_folder_summary_uid (folder->summary, uid);
	CamelMimeMessage *msg;
	CamelException ex;

	camel_exception_init(&ex);
	msg = camel_folder_get_message (folder, uid, &ex);
	if (camel_exception_is_set(&ex))
		g_warning ("Error fetching message: %s", camel_exception_get_description(&ex));
	else {
		if (camel_mime_message_build_preview ((CamelMimePart *)msg, (CamelMessageInfo *)info) && info->preview)
			camel_db_write_preview_record (folder->parent_store->cdb_w, folder->full_name, info->uid, info->preview, NULL);
	}
	camel_exception_clear(&ex);
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
	gint i;

	preview_data = camel_db_get_folder_preview (m->folder->parent_store->cdb_r, m->folder->full_name, NULL);
	if (preview_data) {
		g_hash_table_foreach_remove (preview_data, (GHRFunc)fill_mi, m->folder);
		g_hash_table_destroy (preview_data);
	}

	CAMEL_SUMMARY_LOCK (m->folder->summary, summary_lock);
	g_hash_table_foreach (m->folder->summary->loaded_infos, (GHFunc)pick_uids, uids_uncached);
	CAMEL_SUMMARY_UNLOCK (m->folder->summary, summary_lock);

	for (i=0; i < uids_uncached->len; i++) {
		g_hash_table_remove (hash, uids_uncached->pdata[i]);
		camel_pstring_free (uids_uncached->pdata[i]); /* unref the hash table key */
	}

	CAMEL_FOLDER_REC_LOCK(m->folder, lock);
	camel_db_begin_transaction (m->folder->parent_store->cdb_w, NULL);
	g_hash_table_foreach (hash, (GHFunc)msg_update_preview, m->folder);
	camel_db_end_transaction (m->folder->parent_store->cdb_w, NULL);
	CAMEL_FOLDER_REC_UNLOCK(m->folder, lock);
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
gint
camel_folder_summary_reload_from_db (CamelFolderSummary *s, CamelException *ex)
{
	CamelDB *cdb;
	gchar *folder_name;
	gint ret = 0;
	struct _db_pass_data data;

	/* FIXME[disk-summary] baseclass this, and vfolders we may have to
	 * load better. */
	d(printf ("\ncamel_folder_summary_reload_from_db called \n"));

	folder_name = s->folder->full_name;
	cdb = s->folder->parent_store->cdb_r;

	/* FIXME FOR SANKAR: No need to pass the address of summary here. */
	data.summary = s;
	data.double_ref = FALSE;
	data.add = FALSE;
	ret = camel_db_read_message_info_records (cdb, folder_name, (gpointer)&data, camel_read_mir_callback, NULL);

	s->cache_load_time = time (NULL);
        /* FIXME[disk-summary] LRU please and not timeouts */
	if (!g_getenv("CAMEL_FREE_INFOS") && !s->timeout_handle)
		s->timeout_handle = g_timeout_add_seconds (SUMMARY_CACHE_DROP, (GSourceFunc) cfs_try_release_memory, s);

	if (_PRIVATE(s)->need_preview) {
		struct _preview_update_msg *m;

		m = camel_session_thread_msg_new(((CamelService *)s->folder->parent_store)->session, &preview_update_ops, sizeof(*m));
		m->folder = s->folder;
		camel_exception_init(&m->ex);
		camel_session_thread_queue(((CamelService *)s->folder->parent_store)->session, &m->msg, 0);
	}

	return ret == 0 ? 0 : -1;
}

void
camel_folder_summary_add_preview (CamelFolderSummary *s, CamelMessageInfo *info)
{
	CAMEL_SUMMARY_LOCK(s, summary_lock);
	g_hash_table_insert (_PRIVATE(s)->preview_updates, (gchar *)info->uid, ((CamelMessageInfoBase *)info)->preview);
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

/**
 * camel_folder_summary_ensure_infos_loaded:
 * @s: #CamelFolderSummary object
 * @at_least: How many infos already loaded are considered fine to not reload all of them.
 *    Use -1 to force reload of all of them if not in memory yet.
 * @ex: #CamelException object.
 *
 * Loads all infos into memory, if they are not yet.
 **/
void
camel_folder_summary_ensure_infos_loaded (CamelFolderSummary *s, gint at_least, CamelException *ex)
{
	guint loaded, known;

	g_return_if_fail (s != NULL);

	loaded = camel_folder_summary_cache_size (s);
	known = camel_folder_summary_count (s);

	if ((at_least == -1 && known != loaded) || at_least > loaded) {
		camel_folder_summary_reload_from_db (s, ex);
	}
}

#if 0
static void
camel_folder_summary_dump (CamelFolderSummary *s)
{
	gint i;

	printf("Dumping %s\n", s->folder ? s->folder->full_name:"nil");
	for (i=0; i<s->uids->len; i++)
		printf("%s\t", (gchar *)s->uids->pdata[i]);
	printf("\n");
}
#endif

GHashTable *
camel_folder_summary_get_flag_cache (CamelFolderSummary *summary)
{
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(summary);

	return p->flag_cache;
}

gint
camel_folder_summary_load_from_db (CamelFolderSummary *s, CamelException *ex)
{
	CamelDB *cdb;
	gchar *folder_name;
	gint ret = 0;
	CamelException ex2;
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);

	/* struct _db_pass_data data; */
	d(printf ("\ncamel_folder_summary_load_from_db called \n"));
	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	ret = camel_folder_summary_header_load_from_db (s, s->folder->parent_store, s->folder->full_name, ex);

	if (ret)
		return ret;

	folder_name = s->folder->full_name;
	cdb = s->folder->parent_store->cdb_r;

	camel_exception_init (&ex2);

	ret = camel_db_get_folder_uids_flags (cdb, folder_name, s->sort_by, s->collate, s->uids, p->flag_cache, &ex2);

	if (camel_exception_is_set (&ex2) && camel_exception_get_description (&ex2) &&
	    strstr (camel_exception_get_description (&ex2), "no such table") != NULL) {
		/* create table the first time it is accessed and missing */
		ret = camel_db_prepare_message_info_table (cdb, folder_name, ex);
	} else if (ex) {
		camel_exception_xfer (ex, &ex2);
	}

	camel_exception_clear (&ex2);

	/* camel_folder_summary_dump (s); */

#if 0
	data.summary = s;
	data.add = TRUE;
	data.double_ref = FALSE;
	ret = camel_db_read_message_info_records (cdb, folder_name, (gpointer) &data, camel_read_mir_callback, ex);
#endif

	return ret == 0 ? 0 : -1;
}

static void
mir_from_cols (CamelMIRecord *mir, CamelFolderSummary *s, gint ncol, gchar ** cols, gchar ** name)
{
	gint i;

	for (i = 0; i < ncol; ++i) {

		if (!strcmp (name [i], "uid"))
			mir->uid = (gchar *) camel_pstring_strdup (cols [i]);
		else if (!strcmp (name [i], "flags"))
			mir->flags = cols [i] ? strtoul (cols [i], NULL, 10) : 0;
		else if (!strcmp (name [i], "read"))
			mir->read =  (cols [i]) ? ( ((strtoul (cols [i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "deleted"))
			mir->deleted = (cols [i]) ? ( ((strtoul (cols [i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "replied"))
			mir->replied = (cols [i]) ? ( ((strtoul (cols [i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "important"))
			mir->important = (cols [i]) ? ( ((strtoul (cols [i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "junk"))
			mir->junk = (cols [i]) ? ( ((strtoul (cols [i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "attachment"))
			mir->attachment = (cols [i]) ? ( ((strtoul (cols [i], NULL, 10)) ? TRUE : FALSE)) : FALSE;
		else if (!strcmp (name [i], "size"))
			mir->size =  cols [i] ? strtoul (cols [i], NULL, 10) : 0;
		else if (!strcmp (name [i], "dsent"))
			mir->dsent = cols [i] ? strtol (cols [i], NULL, 10) : 0;
		else if (!strcmp (name [i], "dreceived"))
			mir->dreceived = cols [i] ? strtol (cols [i], NULL, 10) : 0;
		else if (!strcmp (name [i], "subject"))
			mir->subject = (gchar *) camel_pstring_strdup (cols [i]);
		else if (!strcmp (name [i], "mail_from"))
			mir->from = (gchar *) camel_pstring_strdup (cols [i]);
		else if (!strcmp (name [i], "mail_to"))
			mir->to = (gchar *) camel_pstring_strdup (cols [i]);
		else if (!strcmp (name [i], "mail_cc"))
			mir->cc = (gchar *) camel_pstring_strdup(cols [i]);
		else if (!strcmp (name [i], "mlist"))
			mir->mlist = (gchar *) camel_pstring_strdup (cols [i]);
		else if (!strcmp (name [i], "followup_flag"))
			mir->followup_flag = (gchar *) camel_pstring_strdup(cols [i]);
		else if (!strcmp (name [i], "followup_completed_on"))
			mir->followup_completed_on = (gchar *) camel_pstring_strdup(cols [i]);
		else if (!strcmp (name [i], "followup_due_by"))
			mir->followup_due_by = (gchar *) camel_pstring_strdup(cols [i]);
		else if (!strcmp (name [i], "part"))
			mir->part = g_strdup (cols [i]);
		else if (!strcmp (name [i], "labels"))
			mir->labels = g_strdup (cols [i]);
		else if (!strcmp (name [i], "usertags"))
			mir->usertags = g_strdup (cols [i]);
		else if (!strcmp (name [i], "cinfo"))
			mir->cinfo = g_strdup(cols [i]);
		else if (!strcmp (name [i], "bdata"))
			mir->bdata = g_strdup(cols [i]);
		/* Evolution itself doesn't yet use this, ignoring
		else if (!strcmp (name [i], "bodystructure"))
			mir->bodystructure = g_strdup(cols [i]); */

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

	CAMEL_SUMMARY_LOCK (s, summary_lock);
	if (g_hash_table_lookup (s->loaded_infos, mir->uid)) {
		/* Unlock and better return*/
		CAMEL_SUMMARY_UNLOCK (s, summary_lock);
		camel_db_camel_mir_free (mir);
		return ret;
	}
	CAMEL_SUMMARY_UNLOCK (s, summary_lock);

	info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_from_db (s, mir);

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

		if (data->double_ref)
			/* double reffing, because, at times frees before, I could read it. so we dont ref and give it again, just use it */
			camel_message_info_ref(info);

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
 * camel_folder_summary_load:
 * @summary: a #CamelFolderSummary object
 *
 * Load the summary from disk.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_folder_summary_load(CamelFolderSummary *s)
{
#if 0
	FILE *in;
	gint i;
	CamelMessageInfo *mi;

	d(g_print ("\ncamel_folder_summary_load from FLAT FILE called \n"));

	if (s->summary_path == NULL ||
	    s->meta_summary->path == NULL)
		return 0;

	in = g_fopen(s->summary_path, "rb");
	if (in == NULL)
		return -1;

	CAMEL_SUMMARY_LOCK(s, io_lock);
	if ( ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_load(s, in) == -1)
		goto error;

	/* now read in each message ... */
	for (i=0;i<s->saved_count;i++) {
		mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_load(s, in);

		if (mi == NULL)
			goto error;

		/* FIXME: this should be done differently, how i don't know */
		if (s->build_content) {
			((CamelMessageInfoBase *)mi)->content = perform_content_info_load(s, in);
			if (((CamelMessageInfoBase *)mi)->content == NULL) {
				camel_message_info_free(mi);
				goto error;
			}
		}

		camel_folder_summary_add (s, mi);
	}

	CAMEL_SUMMARY_UNLOCK(s, io_lock);

	if (fclose (in) != 0)
		return -1;

	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	return 0;

error:
	if (errno != EINVAL)
		g_warning ("Cannot load summary file: '%s': %s", s->summary_path, g_strerror (errno));

	CAMEL_SUMMARY_UNLOCK(s, io_lock);
	fclose (in);
	s->flags |= ~CAMEL_SUMMARY_DIRTY;
#endif
	return -1;

}

gint
camel_folder_summary_migrate_infos(CamelFolderSummary *s)
{
	FILE *in;
	gint i;
	CamelMessageInfo *mi;
	CamelMessageInfoBase *info;
	gint ret = 0;
	CamelDB *cdb = s->folder->parent_store->cdb_w;
	CamelFIRecord *record;
	CamelException ex;

	/* Kick off the gc thread cycle. */
	if (s->timeout_handle)
		g_source_remove (s->timeout_handle);
	s->timeout_handle = 0;

	camel_exception_init (&ex);
	d(g_print ("\ncamel_folder_summary_load from FLAT FILE called \n"));

	if (s->summary_path == NULL) {
		g_warning ("No summary path set. Unable to migrate\n");
		return -1;
	}

	in = g_fopen(s->summary_path, "rb");
	if (in == NULL)
		return -1;

	if ( ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_load(s, in) == -1)
		goto error;

	/* now read in each message ... */
	for (i=0;i<s->saved_count;i++) {
		CamelTag *tag;

		mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_load(s, in);

		if (mi == NULL)
			goto error;

		/* FIXME: this should be done differently, how i don't know */
		if (s->build_content) {
			((CamelMessageInfoBase *)mi)->content = perform_content_info_load(s, in);
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

	record = (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_to_db (s, &ex));
	if (!record) {
		return -1;
	}

	ret = save_message_infos_to_db (s, TRUE, &ex);

	if (ret != 0) {
		return -1;
	}

	camel_db_begin_transaction (cdb, &ex);
	ret = camel_db_write_folder_info_record (cdb, record, &ex);
	camel_db_end_transaction (cdb, &ex);

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

	if (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS (s)))->content_info_to_db (s, ci, record) == -1)
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

/* saves the content descriptions, recursively */
#if 0
static gint
perform_content_info_save(CamelFolderSummary *s, FILE *out, CamelMessageContentInfo *ci)
{
	CamelMessageContentInfo *part;

	if (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS (s)))->content_info_save (s, out, ci) == -1)
		return -1;

	if (camel_file_util_encode_uint32 (out, my_list_size ((struct _node **)&ci->childs)) == -1)
		return -1;

	part = ci->childs;
	while (part) {
		if (perform_content_info_save (s, out, part) == -1)
			return -1;
		part = part->next;
	}

	return 0;
}
#endif

typedef struct {
	CamelException *ex;
	gboolean migration;
	gint progress;
} SaveToDBArgs;

static void
save_to_db_cb (gpointer key, gpointer value, gpointer data)
{
	SaveToDBArgs *args = (SaveToDBArgs *) data;
	CamelException *ex = args->ex;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)value;
	CamelFolderSummary *s = (CamelFolderSummary *)mi->summary;
	gchar *folder_name = s->folder->full_name;
	CamelDB *cdb = s->folder->parent_store->cdb_w;
	CamelMIRecord *mir;

	if (!args->migration && !mi->dirty)
		return;

	mir = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_to_db (s, (CamelMessageInfo *)mi);

	if (mir && s->build_content) {
		if (perform_content_info_save_to_db (s, ((CamelMessageInfoBase *)mi)->content, mir) == -1) {
			g_warning ("unable to save mir+cinfo for uid: %s\n", mir->uid);
			camel_db_camel_mir_free (mir);
			/* FIXME: Add exception here */
			return;
		}
	}

	if (!args->migration) {
			if (camel_db_write_message_info_record (cdb, folder_name, mir, ex) != 0) {
					camel_db_camel_mir_free (mir);
					return;
			}
	} else {
			if (camel_db_write_fresh_message_info_record (cdb, CAMEL_DB_IN_MEMORY_TABLE, mir, ex) != 0) {
					camel_db_camel_mir_free (mir);
					return;
			}

			if (args->progress > CAMEL_DB_IN_MEMORY_TABLE_LIMIT) {
			    g_print ("BULK INsert limit reached \n");
				camel_db_flush_in_memory_transactions (cdb, folder_name, ex);
				camel_db_start_in_memory_transactions (cdb, ex);
				args->progress = 0;
			} else {
				args->progress ++;
			}
	}

	/* Reset the dirty flag which decides if the changes are synced to the DB or not.
	The FOLDER_FLAGGED should be used to check if the changes are synced to the server.
	So, dont unset the FOLDER_FLAGGED flag */
	mi->dirty = FALSE;

	camel_db_camel_mir_free (mir);
}

static gint
save_message_infos_to_db (CamelFolderSummary *s, gboolean fresh_mirs, CamelException *ex)
{
	CamelDB *cdb = s->folder->parent_store->cdb_w;
	gchar *folder_name;
	SaveToDBArgs args;

	args.ex = ex;
	args.migration = fresh_mirs;
	args.progress = 0;

	folder_name = s->folder->full_name;
	if (camel_db_prepare_message_info_table (cdb, folder_name, ex) != 0) {
		return -1;
	}
	CAMEL_SUMMARY_LOCK(s, summary_lock);
	/* Push MessageInfo-es */
	g_hash_table_foreach (s->loaded_infos, save_to_db_cb, &args);
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
/* FIXME[disk-summary] make sure we free the message infos that are loaded
 * are freed if not used anymore or should we leave that to the timer? */

	return 0;
}

static void
msg_save_preview (const gchar *uid, gpointer value, CamelFolder *folder)
{
	camel_db_write_preview_record (folder->parent_store->cdb_w, folder->full_name, uid, (gchar *)value, NULL);
}

gint
camel_folder_summary_save_to_db (CamelFolderSummary *s, CamelException *ex)
{
	CamelDB *cdb = s->folder->parent_store->cdb_w;
	CamelFIRecord *record;
	gint ret, count;

	d(printf ("\ncamel_folder_summary_save_to_db called \n"));
	if (_PRIVATE(s)->need_preview && g_hash_table_size(_PRIVATE(s)->preview_updates)) {
		camel_db_begin_transaction (s->folder->parent_store->cdb_w, NULL);
		CAMEL_SUMMARY_LOCK(s, summary_lock);
		g_hash_table_foreach (_PRIVATE(s)->preview_updates, (GHFunc)msg_save_preview, s->folder);
		g_hash_table_remove_all (_PRIVATE(s)->preview_updates);
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		camel_db_end_transaction (s->folder->parent_store->cdb_w, NULL);
	}

	if (!(s->flags & CAMEL_SUMMARY_DIRTY))
		return 0;

	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	count = cfs_count_dirty(s);
	if (!count)
		return camel_folder_summary_header_save_to_db (s, ex);

	dd(printf("Saving %d/%d dirty records of %s\n", count, g_hash_table_size (s->loaded_infos), s->folder->full_name));

	camel_db_begin_transaction (cdb, ex);

	ret = save_message_infos_to_db (s, FALSE, ex);
	if (ret != 0) {
		camel_db_abort_transaction (cdb, ex);
		/* Failed, so lets reset the flag */
		s->flags |= CAMEL_SUMMARY_DIRTY;
		return -1;
	}

	if (ex && camel_exception_is_set (ex) && strstr (camel_exception_get_description (ex), "26 columns but 28 values") != NULL) {
		/* This is an error is previous migration. Let remigrate this folder alone. */
		camel_db_abort_transaction (cdb, ex);
		camel_db_reset_folder_version (cdb, s->folder->full_name, 0, ex);
		g_warning ("Fixing up a broken summary migration on %s\n", s->folder->full_name);
		/* Begin everything again. */
		camel_db_begin_transaction (cdb, ex);

		ret = save_message_infos_to_db (s, FALSE, ex);
		if (ret != 0) {
			camel_db_abort_transaction (cdb, ex);
			/* Failed, so lets reset the flag */
			s->flags |= CAMEL_SUMMARY_DIRTY;
			return -1;
		}
	}

	camel_db_end_transaction (cdb, ex);

	record = (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_to_db (s, ex));
	if (!record) {
		s->flags |= CAMEL_SUMMARY_DIRTY;
		return -1;
	}

	camel_db_begin_transaction (cdb, ex);
	ret = camel_db_write_folder_info_record (cdb, record, ex);
	g_free (record->bdata);
	g_free (record);

	if (ret != 0) {
		camel_db_abort_transaction (cdb, ex);
		s->flags |= CAMEL_SUMMARY_DIRTY;
		return -1;
	}

	camel_db_end_transaction (cdb, ex);

	return ret;
}

gint
camel_folder_summary_header_save_to_db (CamelFolderSummary *s, CamelException *ex)
{
	CamelDB *cdb = s->folder->parent_store->cdb_w;
	CamelFIRecord *record;
	gint ret;

	d(printf ("\ncamel_folder_summary_header_save_to_db called \n"));

	record = (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_to_db (s, ex));
	if (!record) {
		return -1;
	}

	camel_db_begin_transaction (cdb, ex);
	ret = camel_db_write_folder_info_record (cdb, record, ex);
	g_free (record->bdata);
	g_free (record);

	if (ret != 0) {
		camel_db_abort_transaction (cdb, ex);
		return -1;
	}

	camel_db_end_transaction (cdb, ex);

	return ret;
}

/**
 * camel_folder_summary_save:
 * @summary: a #CamelFolderSummary object
 *
 * Writes the summary to disk.  The summary is only written if changes
 * have occured.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_folder_summary_save(CamelFolderSummary *s)
{
#if 0
	FILE *out;
	FILE *out_meta;
	gint fd, i, fd_meta;
	guint32 count;
	CamelMessageInfo *mi;
	gchar *path;
	gchar *path_meta;

	g_assert(s->message_info_size >= sizeof(CamelMessageInfoBase));

	if (s->summary_path == NULL
	    || s->meta_summary->path == NULL
	    || (s->flags & CAMEL_SUMMARY_DIRTY) == 0)
		return 0;

	path = alloca(strlen(s->summary_path)+4);
	sprintf(path, "%s~", s->summary_path);
	fd = g_open(path, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);
	if (fd == -1)
		return -1;
	out = fdopen(fd, "wb");
	if (out == NULL) {
		i = errno;
		g_unlink(path);
		close(fd);
		errno = i;
		return -1;
	}

	/* Meta summary code */
	/* This meta summary will be used by beagle in order to
	   quickly pass through the actual summary file, which
	   is quite time consuming otherwise.
	*/
	/* FIXME: Merge meta-summary and summary */
	path_meta = alloca(strlen(s->meta_summary->path)+4);
	sprintf(path_meta, "%s~", s->meta_summary->path);
	fd_meta = g_open(path_meta, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600);
	if (fd_meta == -1) {
		fclose(out);
		return -1;
	}
	out_meta = fdopen(fd_meta, "wb");
	if (out_meta == NULL) {
		i = errno;
		g_unlink(path);
		g_unlink(path_meta);
		fclose(out);
		close(fd_meta);
		errno = i;
		return -1;
	}

	io(printf("saving header\n"));

	CAMEL_SUMMARY_LOCK(s, io_lock);

	if (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_save(s, out) == -1)
		goto exception;

	if (summary_meta_header_save(s, out_meta) == -1)
		goto exception;

	/* now write out each message ... */
	/* we check ferorr when done for i/o errors */

	count = s->messages->len;
	for (i = 0; i < count; i++) {
		mi = s->messages->pdata[i];
		if (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS (s)))->meta_message_info_save (s, out_meta, out, mi) == -1)
			goto exception;

		if (((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS (s)))->message_info_save (s, out, mi) == -1)
			goto exception;

		if (s->build_content) {
			if (perform_content_info_save (s, out, ((CamelMessageInfoBase *)mi)->content) == -1)
				goto exception;
		}
	}

	/* FIXME: Can't we use the above "fd" variables, instead of fileno()? */
	if (fflush (out) != 0 || fsync (fileno (out)) == -1)
		goto exception;

	if (fflush (out_meta) != 0 || fsync (fileno (out_meta)) == -1)
		goto exception;

	fclose (out);
	fclose (out_meta);

	CAMEL_SUMMARY_UNLOCK(s, io_lock);

#ifdef G_OS_WIN32
	g_unlink(s->summary_path);
#endif

	if (g_rename(path, s->summary_path) == -1) {
		i = errno;
		g_unlink(path);
		errno = i;
		return -1;
	}

	if (g_rename(path_meta, s->meta_summary->path) == -1) {
		i = errno;
		g_unlink(path_meta);
		errno = i;
		return -1;
	}

	s->flags &= ~CAMEL_SUMMARY_DIRTY;
	return 0;

exception:

	i = errno;
	fclose (out);
	fclose (out_meta);

	CAMEL_SUMMARY_UNLOCK(s, io_lock);

	g_unlink (path);
	g_unlink (path_meta);
	errno = i;
#endif
	return -1;
}

gint
camel_folder_summary_header_load_from_db (CamelFolderSummary *s, CamelStore *store, const gchar *folder_name, CamelException *ex)
{
	CamelDB *cdb;
	CamelFIRecord *record;
	gint ret = 0;

	d(printf ("\ncamel_folder_summary_load_from_db called \n"));
	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	cdb = store->cdb_r;

	record = g_new0 (CamelFIRecord, 1);
	camel_db_read_folder_info_record (cdb, folder_name, &record, ex);

	if (record) {
		if ( ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_from_db (s, record) == -1)
			ret = -1;
	} else {
		ret = -1;
	}

	g_free (record->folder_name);
	g_free (record->bdata);
	g_free (record);

	return ret;
}

/**
 * camel_folder_summary_header_load:
 * @summary: a #CamelFolderSummary object
 *
 * Only load the header information from the summary,
 * keep the rest on disk.  This should only be done on
 * a fresh summary object.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_folder_summary_header_load(CamelFolderSummary *s)
{
	gint ret=-1;
#if 0
	FILE *in;
	FILE *in_meta;

	if (s->summary_path == NULL ||
	    s->meta_summary->path == NULL)
		return 0;

	in = g_fopen(s->summary_path, "rb");
	if (in == NULL)
		return -1;

	in_meta = g_fopen(s->meta_summary->path, "rb");
	if (in_meta == NULL) {
		fclose(in);
		return -1;
	}

	CAMEL_SUMMARY_LOCK(s, io_lock);
	ret = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->summary_header_load(s, in);
	ret = summary_meta_header_load(s, in_meta);
	CAMEL_SUMMARY_UNLOCK(s, io_lock);

	fclose(in);
	fclose(in_meta);
	s->flags &= ~CAMEL_SUMMARY_DIRTY;
#endif
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

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	while ((mi = g_hash_table_lookup(s->loaded_infos, uid))) {
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);

		if (mi == info)
			return 0;

		d(printf ("Trying to insert message with clashing uid (%s).  new uid re-assigned", camel_message_info_uid (info)));

		camel_pstring_free (info->uid);
		uid = info->uid = camel_pstring_add (camel_folder_summary_next_uid_string(s), TRUE);
		camel_message_info_set_flags(info, CAMEL_MESSAGE_FOLDER_FLAGGED, CAMEL_MESSAGE_FOLDER_FLAGGED);

		CAMEL_SUMMARY_LOCK(s, summary_lock);
	}

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

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

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	/* Summary always holds a ref for the loaded infos */
	/* camel_message_info_ref(info); FIXME: Check how things are loaded. */
	/* FIXME[disk-summary] SHould we ref it or redesign it later on */
	/* The uid array should have its own memory. We will unload the infos when not reqd.*/
	g_ptr_array_add (s->uids, (gpointer) camel_pstring_strdup((camel_message_info_uid(info))));
	g_hash_table_replace (_PRIVATE(s)->flag_cache, (gchar *)info->uid, GUINT_TO_POINTER(camel_message_info_flags(info)));

	g_hash_table_insert (s->loaded_infos, (gpointer) camel_message_info_uid (info), info);
	s->flags |= CAMEL_SUMMARY_DIRTY;

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

void
camel_folder_summary_insert (CamelFolderSummary *s, CamelMessageInfo *info, gboolean load)
{
	if (info == NULL)
		return;

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	/* Summary always holds a ref for the loaded infos */
	/* camel_message_info_ref(info); FIXME: Check how things are loaded. */
	/* FIXME[disk-summary] SHould we ref it or redesign it later on */
	/* The uid array should have its own memory. We will unload the infos when not reqd.*/
	if (!load)
		g_ptr_array_add (s->uids, (gchar *) camel_pstring_strdup(camel_message_info_uid(info)));

	g_hash_table_insert (s->loaded_infos, (gchar *) camel_message_info_uid (info), info);
	if (load) {
		g_hash_table_replace (_PRIVATE(s)->flag_cache, (gchar *)info->uid, GUINT_TO_POINTER(camel_message_info_flags(info)));
	}

	if (!load)
		s->flags |= CAMEL_SUMMARY_DIRTY;

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

static void
update_summary (CamelFolderSummary *summary, CamelMessageInfoBase *info)
{
	gint unread=0, deleted=0, junk=0;
	guint32 flags = info->flags;

	if (!(flags & CAMEL_MESSAGE_SEEN))
		unread = 1;

	if (flags & CAMEL_MESSAGE_DELETED)
		deleted = 1;

	if (flags & CAMEL_MESSAGE_JUNK)
		junk = 1;

	dd(printf("%p: %d %d %d | %d %d %d \n", (gpointer) summary, unread, deleted, junk, summary->unread_count, summary->visible_count, summary->saved_count));
	info->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
	info->dirty = TRUE;

	if (summary) {

		if (unread)
			summary->unread_count += unread;
		if (deleted)
			summary->deleted_count += deleted;
		if (junk)
			summary->junk_count += junk;
		if (junk && !deleted)
			summary->junk_not_deleted_count += junk;
		summary->visible_count++;
		if (junk ||  deleted)
			summary->visible_count -= junk ? junk : deleted;

		summary->saved_count++;
		camel_folder_summary_touch(summary);
	}

	dd(printf("%p: %d %d %d | %d %d %d\n", (gpointer) summary, unread, deleted, junk, summary->unread_count, summary->visible_count, summary->saved_count));

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
camel_folder_summary_add_from_header(CamelFolderSummary *s, struct _camel_header_raw *h)
{
	CamelMessageInfo *info = camel_folder_summary_info_new_from_header(s, h);

	camel_folder_summary_add (s, info);
	update_summary (s, (CamelMessageInfoBase *) info);
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
camel_folder_summary_info_new_from_header(CamelFolderSummary *s, struct _camel_header_raw *h)
{
	return ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_header(s, h);
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
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);
	off_t start;
	CamelIndexName *name = NULL;

	/* should this check the parser is in the right state, or assume it is?? */

	start = camel_mime_parser_tell(mp);
	if (camel_mime_parser_step(mp, &buffer, &len) != CAMEL_MIME_PARSER_STATE_EOF) {
		info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_parser(s, mp);

		camel_mime_parser_unstep(mp);

		/* assign a unique uid, this is slightly 'wrong' as we do not really
		 * know if we are going to store this in the summary, but no matter */
		if (p->index)
			summary_assign_uid(s, info);

		CAMEL_SUMMARY_LOCK(s, filter_lock);

		if (p->index) {
			if (p->filter_index == NULL)
				p->filter_index = camel_mime_filter_index_new_index(p->index);
			camel_index_delete_name(p->index, camel_message_info_uid(info));
			name = camel_index_add_name(p->index, camel_message_info_uid(info));
			camel_mime_filter_index_set_name(p->filter_index, name);
		}

		/* always scan the content info, even if we dont save it */
		((CamelMessageInfoBase *)info)->content = summary_build_content_info(s, info, mp);

		if (name && p->index) {
			camel_index_write_name(p->index, name);
			camel_object_unref((CamelObject *)name);
			camel_mime_filter_index_set_name(p->filter_index, NULL);
		}

		CAMEL_SUMMARY_UNLOCK(s, filter_lock);

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
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);
	CamelIndexName *name = NULL;

	info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_message(s, msg, bodystructure);

	/* assign a unique uid, this is slightly 'wrong' as we do not really
	 * know if we are going to store this in the summary, but we need it set for indexing */
	if (p->index)
		summary_assign_uid(s, info);

	CAMEL_SUMMARY_LOCK(s, filter_lock);

	if (p->index) {
		if (p->filter_index == NULL)
			p->filter_index = camel_mime_filter_index_new_index(p->index);
		camel_index_delete_name(p->index, camel_message_info_uid(info));
		name = camel_index_add_name(p->index, camel_message_info_uid(info));
		camel_mime_filter_index_set_name(p->filter_index, name);

		if (p->filter_stream == NULL) {
			CamelStream *null = camel_stream_null_new();

			p->filter_stream = camel_stream_filter_new_with_stream(null);
			camel_object_unref((CamelObject *)null);
		}
	}

	((CamelMessageInfoBase *)info)->content = summary_build_content_info_message(s, info, (CamelMimePart *)msg);

	if (name) {
		camel_index_write_name(p->index, name);
		camel_object_unref((CamelObject *)name);
		camel_mime_filter_index_set_name(p->filter_index, NULL);
	}

	CAMEL_SUMMARY_UNLOCK(s, filter_lock);

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
	((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_free(s, ci);
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
	CAMEL_SUMMARY_LOCK(s, summary_lock);
	s->flags |= CAMEL_SUMMARY_DIRTY;
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
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

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	if (camel_folder_summary_count(s) == 0) {
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		return;
	}

	g_ptr_array_foreach (s->uids, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (s->uids, TRUE);
	s->uids = g_ptr_array_new ();
	s->visible_count = s->deleted_count = s->unread_count = 0;

	g_hash_table_destroy(s->loaded_infos);
	s->loaded_infos = g_hash_table_new(g_str_hash, g_str_equal);

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);
}

/* FIXME: This is non-sense. Neither an exception is passed,
nor a value returned. How is the caller supposed to know,
whether the operation is succesful */

void
camel_folder_summary_clear_db (CamelFolderSummary *s)
{
	CamelDB *cdb;
	gchar *folder_name;

	d(printf ("\ncamel_folder_summary_load_from_db called \n"));
	s->flags &= ~CAMEL_SUMMARY_DIRTY;

	folder_name = s->folder->full_name;
	cdb = s->folder->parent_store->cdb_w;

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	if (camel_folder_summary_count(s) == 0) {
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		return;
	}

	g_ptr_array_foreach (s->uids, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (s->uids, TRUE);
	s->uids = g_ptr_array_new ();
	s->visible_count = s->deleted_count = s->unread_count = 0;

	g_hash_table_destroy(s->loaded_infos);
	s->loaded_infos = g_hash_table_new(g_str_hash, g_str_equal);

	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

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
	gboolean found;
	gint ret;

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	found = g_hash_table_lookup (s->loaded_infos, camel_message_info_uid (info)) != NULL;
	g_hash_table_remove (s->loaded_infos, camel_message_info_uid(info));
	ret = summary_remove_uid (s, camel_message_info_uid(info));

	s->flags |= CAMEL_SUMMARY_DIRTY;
	s->meta_summary->msg_expunged = TRUE;
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	if (!ret && camel_db_delete_uid (s->folder->parent_store->cdb_w, s->folder->full_name, camel_message_info_uid(info), NULL) != 0)
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

		CAMEL_SUMMARY_LOCK(s, summary_lock);
		CAMEL_SUMMARY_LOCK(s, ref_lock);
		if (g_hash_table_lookup_extended(s->loaded_infos, uid, (gpointer)&olduid, (gpointer)&oldinfo)) {
				/* make sure it doesn't vanish while we're removing it */
				oldinfo->refcount++;
				CAMEL_SUMMARY_UNLOCK(s, ref_lock);
				CAMEL_SUMMARY_UNLOCK(s, summary_lock);
				camel_folder_summary_remove(s, oldinfo);
				camel_message_info_free(oldinfo);
		} else {
				gchar *tmpid = g_strdup (uid);
				gint ret;
				/* Info isn't loaded into the memory. We must just remove the UID*/
				ret = summary_remove_uid (s, uid);
				CAMEL_SUMMARY_UNLOCK(s, ref_lock);
				CAMEL_SUMMARY_UNLOCK(s, summary_lock);

				if (!ret && camel_db_delete_uid (s->folder->parent_store->cdb_w, s->folder->full_name, tmpid, NULL) != 0) {
						g_free(tmpid);
						return;
				}
				g_free (tmpid);
		}
}

/* _fast doesn't deal with db and leaves it to the caller. */
void
camel_folder_summary_remove_uid_fast (CamelFolderSummary *s, const gchar *uid)
{
		CamelMessageInfo *oldinfo;
		gchar *olduid;

		CAMEL_SUMMARY_LOCK(s, summary_lock);
		CAMEL_SUMMARY_LOCK(s, ref_lock);
		if (g_hash_table_lookup_extended(s->loaded_infos, uid, (gpointer)&olduid, (gpointer)&oldinfo)) {
				/* make sure it doesn't vanish while we're removing it */
				oldinfo->refcount++;
				CAMEL_SUMMARY_UNLOCK(s, ref_lock);
				g_hash_table_remove (s->loaded_infos, olduid);
				summary_remove_uid (s, olduid);
				s->flags |= CAMEL_SUMMARY_DIRTY;
				s->meta_summary->msg_expunged = TRUE;
				camel_message_info_free(oldinfo);
				camel_message_info_free(oldinfo);
				CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		} else {
				gchar *tmpid = g_strdup (uid);
				/* Info isn't loaded into the memory. We must just remove the UID*/
				summary_remove_uid (s, uid);
				CAMEL_SUMMARY_UNLOCK(s, ref_lock);
				CAMEL_SUMMARY_UNLOCK(s, summary_lock);
				g_free (tmpid);
		}
}

void
camel_folder_summary_remove_index_fast (CamelFolderSummary *s, gint index)
{
	const gchar *uid = s->uids->pdata[index];
        CamelMessageInfo *oldinfo;
        gchar *olduid;

	CAMEL_SUMMARY_LOCK(s, summary_lock);
	CAMEL_SUMMARY_LOCK(s, ref_lock);

	if (g_hash_table_lookup_extended(s->loaded_infos, uid, (gpointer)&olduid, (gpointer)&oldinfo)) {
		/* make sure it doesn't vanish while we're removing it */
		g_hash_table_remove (s->loaded_infos, uid);
		camel_pstring_free (uid);
		g_ptr_array_remove_index(s->uids, index);
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
		camel_message_info_free(oldinfo);
	} else {
		/* Info isn't loaded into the memory. We must just remove the UID*/
		g_ptr_array_remove_index(s->uids, index);
		camel_pstring_free (uid);
		CAMEL_SUMMARY_UNLOCK(s, ref_lock);
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);

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

	CAMEL_SUMMARY_LOCK(s, summary_lock);

	if (start < s->uids->len) {

		gint i;
		CamelDB *cdb;
		CamelException ex; /* May be this should come from the caller  */
		gchar *folder_name;
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
		camel_exception_init (&ex);

		folder_name = s->folder->full_name;
		cdb = s->folder->parent_store->cdb_w;

		/* FIXME[disk-summary] lifecycle of infos should be checked.
		 * Add should add to db and del should del to db. Sync only
		 * the changes at interval and remove those full sync on
		 * folder switch */
		camel_db_delete_uids (cdb, folder_name, uids, &ex);

		g_slist_foreach (uids, (GFunc) camel_pstring_free, NULL);
		g_slist_free (uids);

		memmove(s->uids->pdata+start, s->uids->pdata+end, (s->uids->len-end)*sizeof(s->uids->pdata[0]));
		g_ptr_array_set_size(s->uids, s->uids->len - (end - start));

		s->flags |= CAMEL_SUMMARY_DIRTY;

		CAMEL_SUMMARY_UNLOCK(s, summary_lock);

		camel_exception_clear (&ex);
	} else {
		CAMEL_SUMMARY_UNLOCK(s, summary_lock);
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

#if 0
static gint
summary_meta_header_load(CamelFolderSummary *s, FILE *in)
{
	if (!s->meta_summary->path)
		return -1;

	fseek(in, 0, SEEK_SET);

	io(printf("Loading meta-header\n"));

	if (camel_file_util_decode_uint32(in, &s->meta_summary->major) == -1
	    || camel_file_util_decode_uint32(in, &s->meta_summary->minor) == -1	    || camel_file_util_decode_uint32(in, &s->meta_summary->uid_len) == -1) {
		return -1;
	}

	return 0;
}
#endif

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

static	CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s, CamelException *ex)
{
	CamelFIRecord * record = g_new0 (CamelFIRecord, 1);
	CamelDB *db;
	gchar *table_name;

	/* Though we are going to read, we do this during write, so lets use it that way */
	db = s->folder->parent_store->cdb_w;
	table_name = s->folder->full_name;

	io(printf("Savining header to db\n"));

	record->folder_name = table_name;

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

#if 0
static gint
summary_meta_header_save(CamelFolderSummary *s, FILE *out_meta)
{
	fseek(out_meta, 0, SEEK_SET);

	/* Save meta-summary header */
	if (s->meta_summary->msg_expunged) {
		s->meta_summary->msg_expunged = FALSE;
		camel_file_util_encode_uint32(out_meta, ++s->meta_summary->major);
		camel_file_util_encode_uint32(out_meta, (s->meta_summary->minor=0));
	} else {
		camel_file_util_encode_uint32(out_meta, s->meta_summary->major);
		camel_file_util_encode_uint32(out_meta, ++s->meta_summary->minor);
	}
	camel_file_util_encode_uint32(out_meta, s->meta_summary->uid_len);

	return ferror(out_meta);
}
#endif

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
		mi = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->message_info_new_from_header(s, camel_mime_parser_headers_raw(mp));
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
		ci = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new_from_header(s, camel_mime_parser_headers_raw(mp));
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
	CamelMessageContentInfo *ci;

	CAMEL_SUMMARY_LOCK(s, alloc_lock);
#ifndef ALWAYS_ALLOC
	if (s->content_info_chunks == NULL)
		s->content_info_chunks = e_memchunk_new(32, s->content_info_size);
	ci = e_memchunk_alloc(s->content_info_chunks);
#else
#ifndef USE_GSLICE
	ci = g_malloc (s->content_info_size);
#else
	ci = g_slice_alloc (s->content_info_size);
#endif
#endif
	CAMEL_SUMMARY_UNLOCK(s, alloc_lock);

	memset(ci, 0, s->content_info_size);
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

static CamelMessageInfo *
message_info_load(CamelFolderSummary *s, FILE *in)
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

static gint
meta_message_info_save(CamelFolderSummary *s, FILE *out_meta, FILE *out, CamelMessageInfo *info)
{
	time_t timestamp;
	off_t offset;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;

	time (&timestamp);
	offset = ftell (out);
	/* FIXME: errno check after ftell */

	camel_file_util_encode_time_t(out_meta, timestamp);
	camel_file_util_encode_fixed_string(out_meta, camel_message_info_uid(mi), s->meta_summary->uid_len);
	camel_file_util_encode_uint32(out_meta, mi->flags);
	camel_file_util_encode_off_t(out_meta, offset);

	return ferror(out);
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

static gint
message_info_save(CamelFolderSummary *s, FILE *out, CamelMessageInfo *info)
{
	guint32 count;
	CamelFlag *flag;
	CamelTag *tag;
	gint i;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;

	io(printf("Saving message info\n"));

	camel_file_util_encode_string(out, camel_message_info_uid(mi));
	camel_file_util_encode_uint32(out, mi->flags);
	camel_file_util_encode_uint32(out, mi->size);
	camel_file_util_encode_time_t(out, mi->date_sent);
	camel_file_util_encode_time_t(out, mi->date_received);
	camel_file_util_encode_string(out, camel_message_info_subject(mi));
	camel_file_util_encode_string(out, camel_message_info_from(mi));
	camel_file_util_encode_string(out, camel_message_info_to(mi));
	camel_file_util_encode_string(out, camel_message_info_cc(mi));
	camel_file_util_encode_string(out, camel_message_info_mlist(mi));

	camel_file_util_encode_fixed_int32(out, mi->message_id.id.part.hi);
	camel_file_util_encode_fixed_int32(out, mi->message_id.id.part.lo);

	if (mi->references) {
		camel_file_util_encode_uint32(out, mi->references->size);
		for (i=0;i<mi->references->size;i++) {
			camel_file_util_encode_fixed_int32(out, mi->references->references[i].id.part.hi);
			camel_file_util_encode_fixed_int32(out, mi->references->references[i].id.part.lo);
		}
	} else {
		camel_file_util_encode_uint32(out, 0);
	}

	count = camel_flag_list_size(&mi->user_flags);
	camel_file_util_encode_uint32(out, count);
	flag = mi->user_flags;
	while (flag) {
		camel_file_util_encode_string(out, flag->name);
		flag = flag->next;
	}

	count = camel_tag_list_size(&mi->user_tags);
	camel_file_util_encode_uint32(out, count);
	tag = mi->user_tags;
	while (tag) {
		camel_file_util_encode_string(out, tag->name);
		camel_file_util_encode_string(out, tag->value);
		tag = tag->next;
	}

	return ferror(out);
}

static void
message_info_free(CamelFolderSummary *s, CamelMessageInfo *info)
{
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

	if (s)
#ifndef ALWAYS_ALLOC
		e_memchunk_free(s->message_info_chunks, mi);
#else
#ifndef USE_GSLICE
		g_free(mi);
#else
		g_slice_free1 (s->message_info_size, mi);
#endif
#endif
	else
#ifndef USE_GSLICE
		g_free(mi);
#else
		g_slice_free (CamelMessageInfoBase, mi);
#endif
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

static CamelMessageContentInfo *
content_info_load(CamelFolderSummary *s, FILE *in)
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
content_info_save(CamelFolderSummary *s, FILE *out, CamelMessageContentInfo *ci)
{
	CamelContentType *ct;
	struct _camel_header_param *hp;

	io(printf("Saving content info\n"));

	ct = ci->type;
	if (ct) {
		camel_folder_summary_encode_token(out, ct->type);
		camel_folder_summary_encode_token(out, ct->subtype);
		camel_file_util_encode_uint32(out, my_list_size((struct _node **)&ct->params));
		hp = ct->params;
		while (hp) {
			camel_folder_summary_encode_token(out, hp->name);
			camel_folder_summary_encode_token(out, hp->value);
			hp = hp->next;
		}
	} else {
		camel_folder_summary_encode_token(out, NULL);
		camel_folder_summary_encode_token(out, NULL);
		camel_file_util_encode_uint32(out, 0);
	}
	camel_folder_summary_encode_token(out, ci->id);
	camel_folder_summary_encode_token(out, ci->description);
	camel_folder_summary_encode_token(out, ci->encoding);

	return camel_file_util_encode_uint32(out, ci->size);
}

static void
content_info_free(CamelFolderSummary *s, CamelMessageContentInfo *ci)
{
	camel_content_type_unref(ci->type);
	g_free(ci->id);
	g_free(ci->description);
	g_free(ci->encoding);
#ifndef ALWAYS_ALLOC
	e_memchunk_free(s->content_info_chunks, ci);
#else
#ifndef USE_GSLICE
	g_free(ci);
#else
	g_slice_free1 (s->content_info_size, ci);
#endif
#endif
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
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);
	CamelMimeFilterCharset *mfc;
	CamelMessageContentInfo *part;
	const gchar *calendar_header;

	d(printf("building content info\n"));

	/* start of this part */
	state = camel_mime_parser_step(mp, &buffer, &len);

	if (s->build_content)
		info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new_from_parser(s, mp);

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
						p->filter_64 = camel_mime_filter_basic_new_type(CAMEL_MIME_FILTER_BASIC_BASE64_DEC);
					else
						camel_mime_filter_reset((CamelMimeFilter *)p->filter_64);
					enc_id = camel_mime_parser_filter_add(mp, (CamelMimeFilter *)p->filter_64);
				} else if (!g_ascii_strcasecmp(encoding, "quoted-printable")) {
					d(printf(" decoding quoted-printable\n"));
					if (p->filter_qp == NULL)
						p->filter_qp = camel_mime_filter_basic_new_type(CAMEL_MIME_FILTER_BASIC_QP_DEC);
					else
						camel_mime_filter_reset((CamelMimeFilter *)p->filter_qp);
					enc_id = camel_mime_parser_filter_add(mp, (CamelMimeFilter *)p->filter_qp);
				} else if (!g_ascii_strcasecmp (encoding, "x-uuencode")) {
					d(printf(" decoding x-uuencode\n"));
					if (p->filter_uu == NULL)
						p->filter_uu = camel_mime_filter_basic_new_type(CAMEL_MIME_FILTER_BASIC_UU_DEC);
					else
						camel_mime_filter_reset((CamelMimeFilter *)p->filter_uu);
					enc_id = camel_mime_parser_filter_add(mp, (CamelMimeFilter *)p->filter_uu);
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
					mfc = camel_mime_filter_charset_new_convert(charset, "UTF-8");
					if (mfc)
						g_hash_table_insert(p->filter_charset, g_strdup(charset), mfc);
				} else {
					camel_mime_filter_reset((CamelMimeFilter *)mfc);
				}
				if (mfc) {
					chr_id = camel_mime_parser_filter_add(mp, (CamelMimeFilter *)mfc);
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
			idx_id = camel_mime_parser_filter_add(mp, (CamelMimeFilter *)p->filter_index);
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
	struct _CamelFolderSummaryPrivate *p = _PRIVATE(s);
	CamelMessageContentInfo *info = NULL, *child;
	CamelContentType *ct;
	const struct _camel_header_raw *header;

	if (s->build_content)
		info = ((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(s)))->content_info_new_from_message(s, object);

	containee = camel_medium_get_content_object(CAMEL_MEDIUM(object));

	if (containee == NULL)
		return info;

	/* TODO: I find it odd that get_part and get_content_object do not
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
			html_id = camel_stream_filter_add(p->filter_stream, (CamelMimeFilter *)p->filter_html);
		}
		idx_id = camel_stream_filter_add(p->filter_stream, (CamelMimeFilter *)p->filter_index);

		camel_data_wrapper_decode_to_stream(containee, (CamelStream *)p->filter_stream);
		camel_stream_flush((CamelStream *)p->filter_stream);

		camel_stream_filter_remove(p->filter_stream, idx_id);
		camel_stream_filter_remove(p->filter_stream, html_id);
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
	CamelMessageInfo *info;

	if (s) {
		CAMEL_SUMMARY_LOCK(s, alloc_lock);
#ifndef ALWAYS_ALLOC
		if (s->message_info_chunks == NULL)
			s->message_info_chunks = e_memchunk_new(32, s->message_info_size);
		info = e_memchunk_alloc0(s->message_info_chunks);
#else
#ifndef USE_GSLICE
		info = g_malloc0(s->message_info_size);
#else
		info = g_slice_alloc0 (s->message_info_size);
#endif
#endif
		CAMEL_SUMMARY_UNLOCK(s, alloc_lock);
	} else {
#ifndef USE_GSLICE
		info = g_malloc0(sizeof(CamelMessageInfoBase));
#else
		info = g_slice_alloc0 (sizeof(CamelMessageInfoBase));
#endif

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
		CAMEL_SUMMARY_LOCK(mi->summary, ref_lock);
		g_assert(mi->refcount >= 1);
		mi->refcount++;
		CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);
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
camel_message_info_new_from_header(CamelFolderSummary *s, struct _camel_header_raw *header)
{
	if (s)
		return ((CamelFolderSummaryClass *)((CamelObject *)s)->klass)->message_info_new_from_header(s, header);
	else
		return message_info_new_from_header(NULL, header);
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
		CAMEL_SUMMARY_LOCK(mi->summary, ref_lock);

		if (mi->refcount >= 1)
			mi->refcount--;
		if (mi->refcount > 0) {
			CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);
			return;
		}

		CAMEL_SUMMARY_UNLOCK(mi->summary, ref_lock);

		/* FIXME: this is kinda busted, should really be handled by message info free */
		if (mi->summary->build_content
		    && ((CamelMessageInfoBase *)mi)->content) {
			camel_folder_summary_content_info_free(mi->summary, ((CamelMessageInfoBase *)mi)->content);
		}

		((CamelFolderSummaryClass *)(CAMEL_OBJECT_GET_CLASS(mi->summary)))->message_info_free(mi->summary, mi);
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
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->message_info_clone(mi->summary, mi);
	else
		return message_info_clone(NULL, mi);
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
		abort();
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
		abort();
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
		abort();
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
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_ptr(mi, id);
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
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_uint32(mi, id);
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
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_time(mi, id);
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
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_user_flag(mi, id);
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
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_user_tag(mi, id);
	else
		return info_user_tag(mi, id);
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

		if (((junk && !(mi->flags & CAMEL_MESSAGE_DELETED)))||  (deleted && !(mi->flags & CAMEL_MESSAGE_JUNK)) )
			mi->summary->visible_count -= junk ? junk : deleted;
	}
	if (mi->uid)
		g_hash_table_replace (_PRIVATE(mi->summary)->flag_cache, (gchar *)mi->uid, GUINT_TO_POINTER(mi->flags));
	if (mi->summary && mi->summary->folder && mi->uid) {
		CamelFolderChangeInfo *changes = camel_folder_change_info_new();

		camel_folder_change_info_change_uid(changes, camel_message_info_uid(info));
		camel_object_trigger_event(mi->summary->folder, "folder_changed", changes);
		camel_folder_change_info_free(changes);
	}

	d(printf("%d %d %d %d %d\n", mi->summary->unread_count, mi->summary->deleted_count, mi->summary->junk_count, mi->summary->junk_not_deleted_count, mi->summary->visible_count));
	return TRUE;
}

void
camel_folder_summary_update_flag_cache (CamelFolderSummary *s, const gchar *uid, guint32 flag)
{
	g_hash_table_replace (_PRIVATE(s)->flag_cache, (gchar *) uid, GUINT_TO_POINTER(flag));
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
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_set_flags(mi, flags, set);
	else
		return info_set_flags(mi, flags, set);
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
		camel_object_trigger_event(mi->summary->folder, "folder_changed", changes);
		camel_folder_change_info_free(changes);
	}

	return res;
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
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_set_user_flag(mi, id, state);
	else
		return info_set_user_flag(mi, id, state);
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
		camel_object_trigger_event(mi->summary->folder, "folder_changed", changes);
		camel_folder_change_info_free(changes);
	}

	return res;
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
		return ((CamelFolderSummaryClass *)((CamelObject *)mi->summary)->klass)->info_set_user_tag(mi, id, val);
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

static void
camel_folder_summary_class_init (CamelFolderSummaryClass *klass)
{
	camel_folder_summary_parent = camel_type_get_global_classfuncs (camel_object_get_type ());

	klass->summary_header_load = summary_header_load;
	klass->summary_header_save = summary_header_save;

	klass->summary_header_from_db = summary_header_from_db;
	klass->summary_header_to_db = summary_header_to_db;
	klass->message_info_from_db = message_info_from_db;
	klass->message_info_to_db = message_info_to_db;
	klass->content_info_from_db = content_info_from_db;
	klass->content_info_to_db = content_info_to_db;

	klass->message_info_new_from_header  = message_info_new_from_header;
	klass->message_info_new_from_parser = message_info_new_from_parser;
	klass->message_info_new_from_message = message_info_new_from_message;
	klass->message_info_load = message_info_load;
	klass->message_info_save = message_info_save;
	klass->meta_message_info_save = meta_message_info_save;
	klass->message_info_free = message_info_free;
	klass->message_info_clone = message_info_clone;
	klass->message_info_from_uid = message_info_from_uid;

	klass->content_info_new_from_header  = content_info_new_from_header;
	klass->content_info_new_from_parser = content_info_new_from_parser;
	klass->content_info_new_from_message = content_info_new_from_message;
	klass->content_info_load = content_info_load;
	klass->content_info_save = content_info_save;
	klass->content_info_free = content_info_free;

	klass->next_uid_string = next_uid_string;

	klass->info_ptr = info_ptr;
	klass->info_uint32 = info_uint32;
	klass->info_time = info_time;
	klass->info_user_flag = info_user_flag;
	klass->info_user_tag = info_user_tag;

#if 0
	klass->info_set_string = info_set_string;
	klass->info_set_uint32 = info_set_uint32;
	klass->info_set_time = info_set_time;
	klass->info_set_ptr = info_set_ptr;
#endif
	klass->info_set_user_flag = info_set_user_flag;
	klass->info_set_user_tag = info_set_user_tag;

	klass->info_set_flags = info_set_flags;

}

/* Utils */
void
camel_folder_summary_set_need_preview (CamelFolderSummary *summary, gboolean preview)
{
	_PRIVATE(summary)->need_preview = preview;
}

gboolean
camel_folder_summary_get_need_preview (CamelFolderSummary *summary)
{
	return _PRIVATE(summary)->need_preview;
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

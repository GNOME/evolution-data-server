/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors:
 *    Michael Zucchi <notzed@ximian.com>
 *    Dan Winship <danw@ximian.com>
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "camel-db.h"
#include "camel-folder.h"
#include "camel-file-utils.h"
#include "camel-string-utils.h"
#include "camel-store.h"

#include "camel-imapx-summary.h"
//#include "camel-imap-utils.h"

#define CAMEL_IMAPX_SUMMARY_VERSION (4)

#define EXTRACT_FIRST_DIGIT(val) val=strtoull (part, &part, 10);
#define EXTRACT_DIGIT(val) if (*part) part++; val=strtoull (part, &part, 10);

static gint summary_header_load (CamelFolderSummary *, FILE *);
static gint summary_header_save (CamelFolderSummary *, FILE *);

static CamelMessageInfo *message_info_load (CamelFolderSummary *s, FILE *in);
static gint message_info_save (CamelFolderSummary *s, FILE *out,
			      CamelMessageInfo *info);
static gboolean info_set_user_flag (CamelMessageInfo *info, const gchar *id, gboolean state);
static CamelMessageContentInfo *content_info_load (CamelFolderSummary *s, FILE *in);
static gint content_info_save (CamelFolderSummary *s, FILE *out,
			      CamelMessageContentInfo *info);

static gint summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir);
static CamelFIRecord * summary_header_to_db (CamelFolderSummary *s, CamelException *ex);
static CamelMIRecord * message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info);
static CamelMessageInfo * message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);
static gint content_info_to_db (CamelFolderSummary *s, CamelMessageContentInfo *info, CamelMIRecord *mir);
static CamelMessageContentInfo * content_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);

static void camel_imapx_summary_class_init (CamelIMAPXSummaryClass *klass);
static void camel_imapx_summary_init       (CamelIMAPXSummary *obj);

static CamelFolderSummaryClass *camel_imapx_summary_parent;

CamelType
camel_imapx_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(
			camel_folder_summary_get_type(), "CamelIMAPXSummary",
			sizeof (CamelIMAPXSummary),
			sizeof (CamelIMAPXSummaryClass),
			(CamelObjectClassInitFunc) camel_imapx_summary_class_init,
			NULL,
			(CamelObjectInitFunc) camel_imapx_summary_init,
			NULL);
	}

	return type;
}

static CamelMessageInfo *
imapx_message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
	CamelIMAPXMessageInfo *to;
	const CamelIMAPXMessageInfo *from = (const CamelIMAPXMessageInfo *)mi;

	to = (CamelIMAPXMessageInfo *)camel_imapx_summary_parent->message_info_clone(s, mi);
	to->server_flags = from->server_flags;

	/* FIXME: parent clone should do this */
	to->info.content = camel_folder_summary_content_info_new(s);

	return (CamelMessageInfo *)to;
}

static void
camel_imapx_summary_class_init (CamelIMAPXSummaryClass *klass)
{
	CamelFolderSummaryClass *cfs_class = (CamelFolderSummaryClass *) klass;

	camel_imapx_summary_parent = CAMEL_FOLDER_SUMMARY_CLASS (camel_type_get_global_classfuncs (camel_folder_summary_get_type()));

	cfs_class->message_info_clone = imapx_message_info_clone;

	cfs_class->summary_header_load = summary_header_load;
	cfs_class->summary_header_save = summary_header_save;
	cfs_class->message_info_load = message_info_load;
	cfs_class->message_info_save = message_info_save;
	cfs_class->content_info_load = content_info_load;
	cfs_class->content_info_save = content_info_save;

	cfs_class->summary_header_to_db = summary_header_to_db;
	cfs_class->summary_header_from_db = summary_header_from_db;
	cfs_class->message_info_to_db = message_info_to_db;
	cfs_class->message_info_from_db = message_info_from_db;
	cfs_class->content_info_to_db = content_info_to_db;
	cfs_class->content_info_from_db = content_info_from_db;

	cfs_class->info_set_user_flag = info_set_user_flag;
}

static void
camel_imapx_summary_init (CamelIMAPXSummary *obj)
{
	CamelFolderSummary *s = (CamelFolderSummary *)obj;

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelIMAPXMessageInfo);
	s->content_info_size = sizeof(CamelIMAPXMessageContentInfo);
}

static gint
sort_uid_cmp (gpointer enc, gint len1, gpointer  data1, gint len2, gpointer data2)
{
	static gchar *sa1=NULL, *sa2=NULL;
	static gint l1=0, l2=0;
	gint a1, a2;

	if (l1 < len1+1) {
		sa1 = g_realloc (sa1, len1+1);
		l1 = len1+1;
	}
	if (l2 < len2+1) {
		sa2 = g_realloc (sa2, len2+1);
		l2 = len2+1;
	}
	strncpy (sa1, data1, len1);sa1[len1] = 0;
	strncpy (sa2, data2, len2);sa2[len2] = 0;

	a1 = strtoul (sa1, NULL, 10);
	a2 = strtoul (sa2, NULL, 10);

	return (a1 < a1) ? -1 : (a1 > a2) ? 1 : 0;
}

static gint
uid_compare (gconstpointer va, gconstpointer vb)
{
	const gchar **sa = (const gchar **)va, **sb = (const gchar **)vb;
	gulong a, b;

	a = strtoul (*sa, NULL, 10);
	b = strtoul (*sb, NULL, 10);
	if (a < b)
		return -1;
	else if (a == b)
		return 0;
	else
		return 1;
}

/**
 * camel_imapx_summary_new:
 * @folder: Parent folder.
 * @filename: the file to store the summary in.
 *
 * This will create a new CamelIMAPXSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Returns: A new CamelIMAPXSummary object.
 **/
CamelFolderSummary *
camel_imapx_summary_new (struct _CamelFolder *folder, const gchar *filename)
{
	CamelFolderSummary *summary = CAMEL_FOLDER_SUMMARY (camel_object_new (camel_imapx_summary_get_type ()));
	CamelException ex;
	camel_exception_init (&ex);

	summary->folder = folder;
	/* Don't do DB sort. Its pretty slow to load */
	if (folder && 0) {
		camel_db_set_collate (folder->parent_store->cdb_r, "uid", "imapx_uid_sort", (CamelDBCollate)sort_uid_cmp);
		summary->sort_by = "uid";
		summary->collate = "imapx_uid_sort";
	}

	camel_folder_summary_set_build_content (summary, TRUE);
	camel_folder_summary_set_filename (summary, filename);

	if (camel_folder_summary_load_from_db (summary, &ex) == -1) {
		/* FIXME: Isn't this dangerous ? We clear the summary
		if it cannot be loaded, for some random reason.
		We need to pass the ex and find out why it is not loaded etc. ? */
		camel_folder_summary_clear_db (summary);
		g_message ("Unable to load summary: %s\n", camel_exception_get_description (&ex));
		camel_exception_clear (&ex);
	}

	g_ptr_array_sort (summary->uids, (GCompareFunc) uid_compare);

	return summary;
}

static gint
summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir)
{
	CamelIMAPXSummary *ims = CAMEL_IMAPX_SUMMARY (s);
	gchar *part;

	if (camel_imapx_summary_parent->summary_header_from_db (s, mir) == -1)
		return -1;

	part = mir->bdata;

	if (part) {
		EXTRACT_FIRST_DIGIT (ims->version)
	}

	if (part) {
		EXTRACT_DIGIT (ims->validity)
	}

	if (ims->version >= 4) {
		if (part)
			EXTRACT_DIGIT (ims->uidnext);
		if (part)
			EXTRACT_DIGIT (ims->modseq);
	}

	if (ims->version > CAMEL_IMAPX_SUMMARY_VERSION) {
		g_warning("Unknown summary version\n");
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static gint
summary_header_load (CamelFolderSummary *s, FILE *in)
{
	CamelIMAPXSummary *ims = CAMEL_IMAPX_SUMMARY (s);
	guint32 validity;
	gint ret;

	if (camel_imapx_summary_parent->summary_header_load (s, in) == -1)
		return -1;

	/* Legacy version */
	if (s->version == 0x30c) {
		ret = camel_file_util_decode_uint32(in, &validity);
		if (!ret)
			ims->validity = validity;
		return ret;
	}

	/* Version 1 */
	if (camel_file_util_decode_fixed_int32(in, (gint32 *) &ims->version) == -1)
		return -1;

	if (ims->version == 2) {
		/* Version 2: for compat with version 2 of the imap4 summary files */
		gint have_mlist;

		if (camel_file_util_decode_fixed_int32 (in, &have_mlist) == -1)
			return -1;
	}

	if (camel_file_util_decode_fixed_int32(in, (gint32 *) &validity) == -1)
		return -1;
	ims->validity = validity;

	/* This is only used for migration; will never be asked to load newer
	   versions of the store format */
	if (ims->version > 3) {
		g_warning("Unknown summary version\n");
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s, CamelException *ex)
{
	CamelIMAPXSummary *ims = CAMEL_IMAPX_SUMMARY(s);
	struct _CamelFIRecord *fir;

	fir = camel_imapx_summary_parent->summary_header_to_db (s, ex);
	if (!fir)
		return NULL;
	fir->bdata = g_strdup_printf ("%d %llu %u %llu", CAMEL_IMAPX_SUMMARY_VERSION,
				      (unsigned long long)ims->validity, ims->uidnext,
				      (unsigned long long)ims->modseq);
	return fir;
}

static gint
summary_header_save (CamelFolderSummary *s, FILE *out)
{
	g_warning("imapx %s called; should never happen!\n", __func__);
	return -1;
}

static CamelMessageInfo *
message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	CamelMessageInfo *info;
	CamelIMAPXMessageInfo *iinfo;

	info = camel_imapx_summary_parent->message_info_from_db (s, mir);
	if (info) {
		gchar *part = g_strdup (mir->bdata), *tmp;
		tmp = part;
		iinfo = (CamelIMAPXMessageInfo *)info;
		EXTRACT_FIRST_DIGIT (iinfo->server_flags)
		g_free (tmp);
	}

	return info;
}

static CamelMessageInfo *
message_info_load (CamelFolderSummary *s, FILE *in)
{
	CamelMessageInfo *info;
	CamelIMAPXMessageInfo *iinfo;

	info = camel_imapx_summary_parent->message_info_load (s, in);
	if (info) {
		iinfo = (CamelIMAPXMessageInfo *)info;

		if (camel_file_util_decode_uint32 (in, &iinfo->server_flags) == -1)
			goto error;
	}

	return info;
error:
	camel_message_info_free(info);
	return NULL;
}

static CamelMIRecord *
message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelIMAPXMessageInfo *iinfo = (CamelIMAPXMessageInfo *)info;
	struct _CamelMIRecord *mir;

	mir = camel_imapx_summary_parent->message_info_to_db (s, info);
	if (mir)
		mir->bdata = g_strdup_printf ("%u", iinfo->server_flags);

	return mir;
}

static gint
message_info_save (CamelFolderSummary *s, FILE *out, CamelMessageInfo *info)
{
	CamelIMAPXMessageInfo *iinfo = (CamelIMAPXMessageInfo *)info;

	if (camel_imapx_summary_parent->message_info_save (s, out, info) == -1)
		return -1;

	return camel_file_util_encode_uint32 (out, iinfo->server_flags);
}

static gboolean
info_set_user_flag (CamelMessageInfo *info, const gchar *id, gboolean state)
{
	gboolean res;

	res = camel_imapx_summary_parent->info_set_user_flag (info, id, state);

	/* there was a change, so do not forget to store it to server */
	if (res)
		((CamelIMAPXMessageInfo *)info)->info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;

	return res;
}

static CamelMessageContentInfo *
content_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	gchar *part = mir->cinfo;
	guint32 type=0;

	if (part) {
		if (*part == ' ')
			part++;
		if (part) {
			EXTRACT_FIRST_DIGIT (type);
		}
	}
	mir->cinfo = part;
	if (type)
		return camel_imapx_summary_parent->content_info_from_db (s, mir);
	else
		return camel_folder_summary_content_info_new (s);
}

static CamelMessageContentInfo *
content_info_load (CamelFolderSummary *s, FILE *in)
{
	if (fgetc (in))
		return camel_imapx_summary_parent->content_info_load (s, in);
	else
		return camel_folder_summary_content_info_new (s);
}

static gint
content_info_to_db (CamelFolderSummary *s, CamelMessageContentInfo *info, CamelMIRecord *mir)
{
	gchar *oldr;
	if (info->type) {
		oldr = mir->cinfo;
		mir->cinfo = oldr ? g_strdup_printf("%s 1", oldr) : g_strdup ("1");
		g_free(oldr);
		return camel_imapx_summary_parent->content_info_to_db (s, info, mir);
	} else {
		oldr = mir->cinfo;
		mir->cinfo = oldr ? g_strdup_printf("%s 0", oldr) : g_strdup ("0");
		g_free(oldr);
		return 0;
	}
}

static gint
content_info_save (CamelFolderSummary *s, FILE *out,
		   CamelMessageContentInfo *info)
{
	if (info->type) {
		fputc (1, out);
		return camel_imapx_summary_parent->content_info_save (s, out, info);
	} else
		return fputc (0, out);
}

void
camel_imapx_summary_add_offline (CamelFolderSummary *summary, const gchar *uid,
				CamelMimeMessage *message,
				const CamelMessageInfo *info)
{
	CamelIMAPXMessageInfo *mi;
	const CamelFlag *flag;
	const CamelTag *tag;

	/* Create summary entry */
	mi = (CamelIMAPXMessageInfo *)camel_folder_summary_info_new_from_message (summary, message, NULL);

	/* Copy flags 'n' tags */
	mi->info.flags = camel_message_info_flags(info);

	flag = camel_message_info_user_flags(info);
	while (flag) {
		camel_message_info_set_user_flag((CamelMessageInfo *)mi, flag->name, TRUE);
		flag = flag->next;
	}
	tag = camel_message_info_user_tags(info);
	while (tag) {
		camel_message_info_set_user_tag((CamelMessageInfo *)mi, tag->name, tag->value);
		tag = tag->next;
	}

	mi->info.size = camel_message_info_size(info);
	mi->info.uid = camel_pstring_strdup (uid);

	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);
}

void
camel_imapx_summary_add_offline_uncached (CamelFolderSummary *summary, const gchar *uid,
					 const CamelMessageInfo *info)
{
	CamelIMAPXMessageInfo *mi;

	mi = camel_message_info_clone(info);
	mi->info.uid = camel_pstring_strdup(uid);

	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);
}

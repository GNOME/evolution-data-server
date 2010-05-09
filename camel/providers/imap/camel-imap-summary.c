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

#include "camel-imap-summary.h"
#include "camel-imap-utils.h"

#define CAMEL_IMAP_SUMMARY_VERSION (3)

#define EXTRACT_FIRST_DIGIT(val) val=strtoul (part, &part, 10);
#define EXTRACT_DIGIT(val) if (*part) part++; val=strtoul (part, &part, 10);

static gint summary_header_load (CamelFolderSummary *, FILE *);
static gint summary_header_save (CamelFolderSummary *, FILE *);

static CamelMessageInfo *message_info_migrate (CamelFolderSummary *s, FILE *in);
static gboolean info_set_user_flag (CamelMessageInfo *info, const gchar *id, gboolean state);
static CamelMessageContentInfo *content_info_migrate (CamelFolderSummary *s, FILE *in);

static gint summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir);
static CamelFIRecord * summary_header_to_db (CamelFolderSummary *s, GError **error);
static CamelMIRecord * message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info);
static CamelMessageInfo * message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);
static gint content_info_to_db (CamelFolderSummary *s, CamelMessageContentInfo *info, CamelMIRecord *mir);
static CamelMessageContentInfo * content_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);

G_DEFINE_TYPE (CamelImapSummary, camel_imap_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static CamelMessageInfo *
imap_message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
	CamelImapMessageInfo *to;
	const CamelImapMessageInfo *from = (const CamelImapMessageInfo *)mi;

	to = (CamelImapMessageInfo *) CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->message_info_clone(s, mi);
	to->server_flags = from->server_flags;

	/* FIXME: parent clone should do this */
	to->info.content = camel_folder_summary_content_info_new(s);

	return (CamelMessageInfo *)to;
}

static void
camel_imap_summary_class_init (CamelImapSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_size = sizeof (CamelImapMessageInfo);
	folder_summary_class->content_info_size = sizeof (CamelImapMessageContentInfo);
	folder_summary_class->message_info_clone = imap_message_info_clone;
	folder_summary_class->summary_header_load = summary_header_load;
	folder_summary_class->summary_header_save = summary_header_save;
	folder_summary_class->message_info_migrate = message_info_migrate;
	folder_summary_class->content_info_migrate = content_info_migrate;
	folder_summary_class->summary_header_to_db = summary_header_to_db;
	folder_summary_class->summary_header_from_db = summary_header_from_db;
	folder_summary_class->message_info_to_db = message_info_to_db;
	folder_summary_class->message_info_from_db = message_info_from_db;
	folder_summary_class->content_info_to_db = content_info_to_db;
	folder_summary_class->content_info_from_db = content_info_from_db;
	folder_summary_class->info_set_user_flag = info_set_user_flag;
}

static void
camel_imap_summary_init (CamelImapSummary *imap_summary)
{
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
 * camel_imap_summary_new:
 * @folder: Parent folder.
 * @filename: the file to store the summary in.
 *
 * This will create a new CamelImapSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Returns: A new CamelImapSummary object.
 **/
CamelFolderSummary *
camel_imap_summary_new (CamelFolder *folder, const gchar *filename)
{
	CamelStore *parent_store;
	CamelFolderSummary *summary;

	parent_store = camel_folder_get_parent_store (folder);

	summary = g_object_new (CAMEL_TYPE_IMAP_SUMMARY, NULL);
	summary->folder = folder;
	/* Don't do DB sort. Its pretty slow to load */
	if (folder && 0) {
		camel_db_set_collate (
			parent_store->cdb_r, "uid", "imap_uid_sort",
			(CamelDBCollate) sort_uid_cmp);
		summary->sort_by = "uid";
		summary->collate = "imap_uid_sort";
	}

	camel_folder_summary_set_build_content (summary, TRUE);
	camel_folder_summary_set_filename (summary, filename);

	if (camel_folder_summary_load_from_db (summary, NULL) == -1) {
		/* FIXME: Isn't this dangerous ? We clear the summary
		if it cannot be loaded, for some random reason.
		We need to pass the ex and find out why it is not loaded etc. ? */
		camel_folder_summary_clear_db (summary);
	}

	g_ptr_array_sort (summary->uids, (GCompareFunc) uid_compare);

	return summary;
}

static gint
summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir)
{
	CamelImapSummary *ims = CAMEL_IMAP_SUMMARY (s);
	gchar *part;

	if (CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->summary_header_from_db (s, mir) == -1)
		return -1;

	part = mir->bdata;

	if (part) {
		EXTRACT_FIRST_DIGIT (ims->version)
	}

	if (part) {
		EXTRACT_DIGIT (ims->validity)
	}

	if (ims->version > CAMEL_IMAP_SUMMARY_VERSION) {
		g_warning("Unkown summary version\n");
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static gint
summary_header_load (CamelFolderSummary *s, FILE *in)
{
	CamelImapSummary *ims = CAMEL_IMAP_SUMMARY (s);

	if (CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->summary_header_load (s, in) == -1)
		return -1;

	/* Legacy version */
	if (s->version == 0x30c)
		return camel_file_util_decode_uint32(in, &ims->validity);

	/* Version 1 */
	if (camel_file_util_decode_fixed_int32(in, (gint32 *) &ims->version) == -1)
		return -1;

	if (ims->version == 2) {
		/* Version 2: for compat with version 2 of the imap4 summary files */
		gint have_mlist;

		if (camel_file_util_decode_fixed_int32 (in, &have_mlist) == -1)
			return -1;
	}

	if (camel_file_util_decode_fixed_int32(in, (gint32 *) &ims->validity) == -1)
		return -1;

	if (ims->version > CAMEL_IMAP_SUMMARY_VERSION) {
		g_warning("Unkown summary version\n");
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s, GError **error)
{
	CamelImapSummary *ims = CAMEL_IMAP_SUMMARY(s);
	struct _CamelFIRecord *fir;

	fir = CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->summary_header_to_db (s, error);
	if (!fir)
		return NULL;
	fir->bdata = g_strdup_printf ("%d %u", CAMEL_IMAP_SUMMARY_VERSION, ims->validity);

	return fir;
}

static gint
summary_header_save (CamelFolderSummary *s, FILE *out)
{
	CamelImapSummary *ims = CAMEL_IMAP_SUMMARY(s);

	if (CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->summary_header_save (s, out) == -1)
		return -1;

	camel_file_util_encode_fixed_int32(out, CAMEL_IMAP_SUMMARY_VERSION);

	return camel_file_util_encode_fixed_int32(out, ims->validity);
}

static CamelMessageInfo *
message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	CamelMessageInfo *info;
	CamelImapMessageInfo *iinfo;

	info = CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->message_info_from_db (s, mir);
	if (info) {
		gchar *part = g_strdup (mir->bdata), *tmp;
		tmp = part;
		iinfo = (CamelImapMessageInfo *)info;
		EXTRACT_FIRST_DIGIT (iinfo->server_flags)
		g_free (tmp);
	}

	return info;
}

static CamelMessageInfo *
message_info_migrate (CamelFolderSummary *s, FILE *in)
{
	CamelMessageInfo *info;
	CamelImapMessageInfo *iinfo;

	info = CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->message_info_migrate (s, in);
	if (info) {
		iinfo = (CamelImapMessageInfo *)info;

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
	CamelImapMessageInfo *iinfo = (CamelImapMessageInfo *)info;
	struct _CamelMIRecord *mir;

	mir = CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->message_info_to_db (s, info);
	if (mir)
		mir->bdata = g_strdup_printf ("%u", iinfo->server_flags);

	return mir;
}

static gboolean
info_set_user_flag (CamelMessageInfo *info, const gchar *id, gboolean state)
{
	gboolean res;

	res = CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->info_set_user_flag (info, id, state);

	/* there was a change, so do not forget to store it to server */
	if (res)
		((CamelImapMessageInfo *)info)->info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;

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
		return CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->content_info_from_db (s, mir);
	else
		return camel_folder_summary_content_info_new (s);
}

static CamelMessageContentInfo *
content_info_migrate (CamelFolderSummary *s, FILE *in)
{
	if (fgetc (in))
		return CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->content_info_migrate (s, in);
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
		return CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->content_info_to_db (s, info, mir);
	} else {
		oldr = mir->cinfo;
		mir->cinfo = oldr ? g_strdup_printf("%s 0", oldr) : g_strdup ("0");
		g_free(oldr);
		return 0;
	}
}

void
camel_imap_summary_add_offline (CamelFolderSummary *summary, const gchar *uid,
				CamelMimeMessage *message,
				const CamelMessageInfo *info)
{
	CamelImapMessageInfo *mi;
	const CamelFlag *flag;
	const CamelTag *tag;

	/* Create summary entry */
	mi = (CamelImapMessageInfo *)camel_folder_summary_info_new_from_message (summary, message, NULL);

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
camel_imap_summary_add_offline_uncached (CamelFolderSummary *summary, const gchar *uid,
					 const CamelMessageInfo *info)
{
	CamelImapMessageInfo *mi;

	mi = camel_message_info_clone(info);
	mi->info.uid = camel_pstring_strdup(uid);

	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);
}

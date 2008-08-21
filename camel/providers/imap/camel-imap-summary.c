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

#include "camel-folder.h"
#include "camel-file-utils.h"
#include "camel-string-utils.h"

#include "camel-imap-summary.h"
#include "camel-imap-utils.h"

#define CAMEL_IMAP_SUMMARY_VERSION (3)

#define EXTRACT_FIRST_DIGIT(val) val=strtoul (part, &part, 10);
#define EXTRACT_DIGIT(val) if (*part) part++; val=strtoul (part, &part, 10);

static int summary_header_load (CamelFolderSummary *, FILE *);
static int summary_header_save (CamelFolderSummary *, FILE *);

static CamelMessageInfo *message_info_load (CamelFolderSummary *s, FILE *in);
static int message_info_save (CamelFolderSummary *s, FILE *out,
			      CamelMessageInfo *info);
static gboolean info_set_user_flag (CamelMessageInfo *info, const char *id, gboolean state);
static CamelMessageContentInfo *content_info_load (CamelFolderSummary *s, FILE *in);
static int content_info_save (CamelFolderSummary *s, FILE *out,
			      CamelMessageContentInfo *info);

static int summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir);
static CamelFIRecord * summary_header_to_db (CamelFolderSummary *s, CamelException *ex);
static CamelMIRecord * message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info);
static CamelMessageInfo * message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);
static int content_info_to_db (CamelFolderSummary *s, CamelMessageContentInfo *info, CamelMIRecord *mir);
static CamelMessageContentInfo * content_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);


static void camel_imap_summary_class_init (CamelImapSummaryClass *klass);
static void camel_imap_summary_init       (CamelImapSummary *obj);

static CamelFolderSummaryClass *camel_imap_summary_parent;

CamelType
camel_imap_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(
			camel_folder_summary_get_type(), "CamelImapSummary",
			sizeof (CamelImapSummary),
			sizeof (CamelImapSummaryClass),
			(CamelObjectClassInitFunc) camel_imap_summary_class_init,
			NULL,
			(CamelObjectInitFunc) camel_imap_summary_init,
			NULL);
	}

	return type;
}

static CamelMessageInfo *
imap_message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
	CamelImapMessageInfo *to;
	const CamelImapMessageInfo *from = (const CamelImapMessageInfo *)mi;

	to = (CamelImapMessageInfo *)camel_imap_summary_parent->message_info_clone(s, mi);
	to->server_flags = from->server_flags;

	/* FIXME: parent clone should do this */
	to->info.content = camel_folder_summary_content_info_new(s);

	return (CamelMessageInfo *)to;
}

static void
camel_imap_summary_class_init (CamelImapSummaryClass *klass)
{
	CamelFolderSummaryClass *cfs_class = (CamelFolderSummaryClass *) klass;

	camel_imap_summary_parent = CAMEL_FOLDER_SUMMARY_CLASS (camel_type_get_global_classfuncs (camel_folder_summary_get_type()));

	cfs_class->message_info_clone = imap_message_info_clone;

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
camel_imap_summary_init (CamelImapSummary *obj)
{
	CamelFolderSummary *s = (CamelFolderSummary *)obj;

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelImapMessageInfo);
	s->content_info_size = sizeof(CamelImapMessageContentInfo);
}

static int 
sort_uid_cmp (void *enc, int len1, void * data1, int len2, void *data2)
{
	char *sa1 = (char*)g_utf8_normalize (data1, len1, G_NORMALIZE_DEFAULT);
	char *sa2 = (char*)g_utf8_normalize (data2, len2, G_NORMALIZE_DEFAULT);
	int a1 = strtoul (sa1, NULL, 10);
	int a2 = strtoul (sa2, NULL, 10);

	g_free(sa1); g_free(sa2);

	return (a1 < a1) ? -1 : (a1 > a2) ? 1 : 0;
}

/**
 * camel_imap_summary_new:
 * @folder: Parent folder.
 * @filename: the file to store the summary in.
 *
 * This will create a new CamelImapSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Return value: A new CamelImapSummary object.
 **/
CamelFolderSummary *
camel_imap_summary_new (struct _CamelFolder *folder, const char *filename)
{
	CamelFolderSummary *summary = CAMEL_FOLDER_SUMMARY (camel_object_new (camel_imap_summary_get_type ()));
	CamelException ex;
	camel_exception_init (&ex);

	summary->folder = folder;
	if (folder)
		camel_db_set_collate (folder->cdb, "uid", "uid_sort", (CamelDBCollate)sort_uid_cmp);

	camel_folder_summary_set_build_content (summary, TRUE);
	camel_folder_summary_set_filename (summary, filename);

	if (camel_folder_summary_load_from_db (summary, &ex) == -1) {
		/* FIXME: Isn't this dangerous ? We clear the summary
		if it cannot be loaded, for some random reason.
		We need to pass the ex and find out why it is not loaded etc. ? */
		camel_folder_summary_clear_db (summary);
		g_warning ("Unable to load summary %s\n", camel_exception_get_description (&ex));
		camel_exception_clear (&ex);
	}

	return summary;
}

static int
summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir)
{
	CamelImapSummary *ims = CAMEL_IMAP_SUMMARY (s);
	char *part;

	if (camel_imap_summary_parent->summary_header_from_db (s, mir) == -1)
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

static int
summary_header_load (CamelFolderSummary *s, FILE *in)
{
	CamelImapSummary *ims = CAMEL_IMAP_SUMMARY (s);
	
	if (camel_imap_summary_parent->summary_header_load (s, in) == -1)
		return -1;

	/* Legacy version */
	if (s->version == 0x30c)
		return camel_file_util_decode_uint32(in, &ims->validity);

	/* Version 1 */
	if (camel_file_util_decode_fixed_int32(in, &ims->version) == -1)
		return -1;
	
	if (ims->version == 2) {
		/* Version 2: for compat with version 2 of the imap4 summary files */
		int have_mlist;
		
		if (camel_file_util_decode_fixed_int32 (in, &have_mlist) == -1)
			return -1;
	}
	
	if (camel_file_util_decode_fixed_int32(in, &ims->validity) == -1)
		return -1;
	
	if (ims->version > CAMEL_IMAP_SUMMARY_VERSION) {
		g_warning("Unkown summary version\n");
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s, CamelException *ex)
{
	CamelImapSummary *ims = CAMEL_IMAP_SUMMARY(s);
	struct _CamelFIRecord *fir;
	
	fir = camel_imap_summary_parent->summary_header_to_db (s, ex);
	if (!fir)
		return NULL;
	fir->bdata = g_strdup_printf ("%d %u", CAMEL_IMAP_SUMMARY_VERSION, ims->validity);

	return fir;
}

static int
summary_header_save (CamelFolderSummary *s, FILE *out)
{
	CamelImapSummary *ims = CAMEL_IMAP_SUMMARY(s);

	if (camel_imap_summary_parent->summary_header_save (s, out) == -1)
		return -1;

	camel_file_util_encode_fixed_int32(out, CAMEL_IMAP_SUMMARY_VERSION);

	return camel_file_util_encode_fixed_int32(out, ims->validity);
}

static CamelMessageInfo *
message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	CamelMessageInfo *info;
	CamelImapMessageInfo *iinfo;

	info = camel_imap_summary_parent->message_info_from_db (s, mir);
	if (info) {
		char *part = g_strdup (mir->bdata), *tmp;
		tmp = part;
		iinfo = (CamelImapMessageInfo *)info;
		EXTRACT_FIRST_DIGIT (iinfo->server_flags)
		g_free (tmp);
	}

	return info;
}

static CamelMessageInfo *
message_info_load (CamelFolderSummary *s, FILE *in)
{
	CamelMessageInfo *info;
	CamelImapMessageInfo *iinfo;

	info = camel_imap_summary_parent->message_info_load (s, in);
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

	mir = camel_imap_summary_parent->message_info_to_db (s, info);
	if (mir) 
		mir->bdata = g_strdup_printf ("%u", iinfo->server_flags);

	return mir;
}

static int
message_info_save (CamelFolderSummary *s, FILE *out, CamelMessageInfo *info)
{
	CamelImapMessageInfo *iinfo = (CamelImapMessageInfo *)info;

	if (camel_imap_summary_parent->message_info_save (s, out, info) == -1)
		return -1;

	return camel_file_util_encode_uint32 (out, iinfo->server_flags);
}

static gboolean
info_set_user_flag (CamelMessageInfo *info, const char *id, gboolean state)
{
	gboolean res;

	res = camel_imap_summary_parent->info_set_user_flag (info, id, state);

	/* there was a change, so do not forget to store it to server */
	if (res)
		((CamelImapMessageInfo *)info)->info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;

	return res;
}

static CamelMessageContentInfo *
content_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	char *part = mir->cinfo;
	guint32 type=0;
	
	if (part) {
		EXTRACT_FIRST_DIGIT (type);
	}
	mir->cinfo = part;
	if (type)
		return camel_imap_summary_parent->content_info_from_db (s, mir);
	else
		return camel_folder_summary_content_info_new (s);
}

static CamelMessageContentInfo *
content_info_load (CamelFolderSummary *s, FILE *in)
{
	if (fgetc (in))
		return camel_imap_summary_parent->content_info_load (s, in);
	else
		return camel_folder_summary_content_info_new (s);
}

static int
content_info_to_db (CamelFolderSummary *s, CamelMessageContentInfo *info, CamelMIRecord *mir)
{
	if (info->type) {
		mir->cinfo = g_strdup ("1");
		return camel_imap_summary_parent->content_info_to_db (s, info, mir);
	} else {
		mir->cinfo = g_strdup ("0");
		return 0;
	}
}

static int
content_info_save (CamelFolderSummary *s, FILE *out,
		   CamelMessageContentInfo *info)
{
	if (info->type) {
		fputc (1, out);
		return camel_imap_summary_parent->content_info_save (s, out, info);
	} else
		return fputc (0, out);
}

void
camel_imap_summary_add_offline (CamelFolderSummary *summary, const char *uid,
				CamelMimeMessage *message,
				const CamelMessageInfo *info)
{
	CamelImapMessageInfo *mi;
	const CamelFlag *flag;
	const CamelTag *tag;

	/* Create summary entry */
	mi = (CamelImapMessageInfo *)camel_folder_summary_info_new_from_message (summary, message);

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
camel_imap_summary_add_offline_uncached (CamelFolderSummary *summary, const char *uid,
					 const CamelMessageInfo *info)
{
	CamelImapMessageInfo *mi;

	mi = camel_message_info_clone(info);
	mi->info.uid = camel_pstring_strdup(uid);

	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);
}

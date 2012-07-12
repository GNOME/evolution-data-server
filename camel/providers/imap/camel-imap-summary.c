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

static gboolean info_set_user_flag (CamelMessageInfo *info, const gchar *id, gboolean state);

static gboolean summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir);
static CamelFIRecord * summary_header_to_db (CamelFolderSummary *s, GError **error);
static CamelMIRecord * message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info);
static CamelMessageInfo * message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);
static gboolean content_info_to_db (CamelFolderSummary *s, CamelMessageContentInfo *info, CamelMIRecord *mir);
static CamelMessageContentInfo * content_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);

G_DEFINE_TYPE (CamelImapSummary, camel_imap_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static CamelMessageInfo *
imap_message_info_clone (CamelFolderSummary *s,
                         const CamelMessageInfo *mi)
{
	CamelImapMessageInfo *to;
	const CamelImapMessageInfo *from = (const CamelImapMessageInfo *) mi;

	to = (CamelImapMessageInfo *) CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->message_info_clone (s, mi);
	to->server_flags = from->server_flags;

	/* FIXME: parent clone should do this */
	to->info.content = camel_folder_summary_content_info_new (s);

	return (CamelMessageInfo *) to;
}

static void
camel_imap_summary_class_init (CamelImapSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_size = sizeof (CamelImapMessageInfo);
	folder_summary_class->content_info_size = sizeof (CamelImapMessageContentInfo);
	folder_summary_class->message_info_clone = imap_message_info_clone;
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
sort_uid_cmp (gpointer enc,
              gint len1,
              gpointer data1,
              gint len2,
              gpointer data2)
{
	static gchar *sa1 = NULL, *sa2 = NULL;
	static gint l1 = 0, l2 = 0;
	gint a1, a2;

	if (l1 < len1 + 1) {
		sa1 = g_realloc (sa1, len1 + 1);
		l1 = len1 + 1;
	}
	if (l2 < len2 + 1) {
		sa2 = g_realloc (sa2, len2 + 1);
		l2 = len2 + 1;
	}
	strncpy (sa1, data1, len1); sa1[len1] = 0;
	strncpy (sa2, data2, len2); sa2[len2] = 0;

	a1 = strtoul (sa1, NULL, 10);
	a2 = strtoul (sa2, NULL, 10);

	return (a1 < a1) ? -1 : (a1 > a2) ? 1 : 0;
}

/**
 * camel_imap_summary_new:
 * @folder: Parent folder.
 *
 * This will create a new CamelImapSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Returns: A new CamelImapSummary object.
 **/
CamelFolderSummary *
camel_imap_summary_new (CamelFolder *folder)
{
	CamelStore *parent_store;
	CamelFolderSummary *summary;

	parent_store = camel_folder_get_parent_store (folder);

	summary = g_object_new (CAMEL_TYPE_IMAP_SUMMARY, "folder", folder, NULL);

	/* Don't do DB sort. Its pretty slow to load */
	if (folder && 0) {
		camel_db_set_collate (
			parent_store->cdb_r, "uid", "imap_uid_sort",
			(CamelDBCollate) sort_uid_cmp);
		summary->sort_by = "uid";
		summary->collate = "imap_uid_sort";
	}

	camel_folder_summary_set_build_content (summary, TRUE);

	if (!camel_folder_summary_load_from_db (summary, NULL)) {
		/* FIXME: Isn't this dangerous ? We clear the summary
		if it cannot be loaded, for some random reason.
		We need to pass the ex and find out why it is not loaded etc. ? */
		camel_folder_summary_clear (summary, NULL);
	}

	return summary;
}

static gboolean
summary_header_from_db (CamelFolderSummary *s,
                        CamelFIRecord *mir)
{
	CamelImapSummary *ims = CAMEL_IMAP_SUMMARY (s);
	gchar *part;

	if (!CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->summary_header_from_db (s, mir))
		return FALSE;

	part = mir->bdata;

	ims->version = bdata_extract_digit (&part);
	ims->validity = bdata_extract_digit (&part);

	if (ims->version > CAMEL_IMAP_SUMMARY_VERSION) {
		g_warning ("Unkown summary version\n");
		errno = EINVAL;
		return FALSE;
	}

	return TRUE;
}

static CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s,
                      GError **error)
{
	CamelImapSummary *ims = CAMEL_IMAP_SUMMARY (s);
	struct _CamelFIRecord *fir;

	fir = CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->summary_header_to_db (s, error);
	if (!fir)
		return NULL;
	fir->bdata = g_strdup_printf ("%d %u", CAMEL_IMAP_SUMMARY_VERSION, ims->validity);

	return fir;
}

static CamelMessageInfo *
message_info_from_db (CamelFolderSummary *s,
                      CamelMIRecord *mir)
{
	CamelMessageInfo *info;
	CamelImapMessageInfo *iinfo;

	info = CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->message_info_from_db (s, mir);
	if (info) {
		gchar *part = mir->bdata;

		iinfo = (CamelImapMessageInfo *) info;
		iinfo->server_flags = bdata_extract_digit (&part);
	}

	return info;
}

static CamelMIRecord *
message_info_to_db (CamelFolderSummary *s,
                    CamelMessageInfo *info)
{
	CamelImapMessageInfo *iinfo = (CamelImapMessageInfo *) info;
	struct _CamelMIRecord *mir;

	mir = CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->message_info_to_db (s, info);
	if (mir)
		mir->bdata = g_strdup_printf ("%u", iinfo->server_flags);

	return mir;
}

static gboolean
info_set_user_flag (CamelMessageInfo *info,
                    const gchar *id,
                    gboolean state)
{
	gboolean res;

	res = CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->info_set_user_flag (info, id, state);

	/* there was a change, so do not forget to store it to server */
	if (res)
		((CamelImapMessageInfo *) info)->info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;

	return res;
}

static CamelMessageContentInfo *
content_info_from_db (CamelFolderSummary *s,
                      CamelMIRecord *mir)
{
	gchar *part = mir->cinfo;
	guint32 type = 0;

	if (part) {
		if (*part == ' ')
			part++;
		if (part) {
			type = bdata_extract_digit (&part);
		}
	}
	mir->cinfo = part;
	if (type)
		return CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->content_info_from_db (s, mir);
	else
		return camel_folder_summary_content_info_new (s);
}

static gboolean
content_info_to_db (CamelFolderSummary *s,
                    CamelMessageContentInfo *info,
                    CamelMIRecord *mir)
{
	gchar *oldr;
	if (info->type) {
		oldr = mir->cinfo;
		mir->cinfo = oldr ? g_strdup_printf ("%s 1", oldr) : g_strdup ("1");
		g_free (oldr);
		return CAMEL_FOLDER_SUMMARY_CLASS (camel_imap_summary_parent_class)->content_info_to_db (s, info, mir);
	} else {
		oldr = mir->cinfo;
		mir->cinfo = oldr ? g_strdup_printf ("%s 0", oldr) : g_strdup ("0");
		g_free (oldr);
		return TRUE;
	}
}

void
camel_imap_summary_add_offline (CamelFolderSummary *summary,
                                const gchar *uid,
                                CamelMimeMessage *message,
                                const CamelMessageInfo *info)
{
	CamelImapMessageInfo *mi;
	const CamelFlag *flag;
	const CamelTag *tag;

	/* Create summary entry */
	mi = (CamelImapMessageInfo *)
		camel_folder_summary_info_new_from_message (
		summary, message, NULL);

	/* Copy flags 'n' tags */
	mi->info.flags = camel_message_info_flags (info);

	flag = camel_message_info_user_flags (info);
	while (flag) {
		camel_message_info_set_user_flag ((CamelMessageInfo *) mi, flag->name, TRUE);
		flag = flag->next;
	}
	tag = camel_message_info_user_tags (info);
	while (tag) {
		camel_message_info_set_user_tag ((CamelMessageInfo *) mi, tag->name, tag->value);
		tag = tag->next;
	}

	mi->info.size = camel_message_info_size (info);
	mi->info.uid = camel_pstring_strdup (uid);

	camel_folder_summary_add (summary, (CamelMessageInfo *) mi);
}

void
camel_imap_summary_add_offline_uncached (CamelFolderSummary *summary,
                                         const gchar *uid,
                                         const CamelMessageInfo *info)
{
	CamelImapMessageInfo *mi;

	mi = camel_message_info_clone (info);
	mi->info.uid = camel_pstring_strdup (uid);

	camel_folder_summary_add (summary, (CamelMessageInfo *) mi);
}

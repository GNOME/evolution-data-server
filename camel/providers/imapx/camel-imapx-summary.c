/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors:
 *    Michael Zucchi <notzed@ximian.com>
 *    Dan Winship <danw@ximian.com>
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <camel/camel.h>

#include "camel-imapx-summary.h"

#define CAMEL_IMAPX_SUMMARY_VERSION (4)

G_DEFINE_TYPE (
	CamelIMAPXSummary,
	camel_imapx_summary,
	CAMEL_TYPE_FOLDER_SUMMARY)

static gboolean
imapx_summary_summary_header_from_db (CamelFolderSummary *s,
                                      CamelFIRecord *mir)
{
	gboolean success;

	/* Chain up to parent's summary_header_from_db() method. */
	success = CAMEL_FOLDER_SUMMARY_CLASS (
		camel_imapx_summary_parent_class)->
		summary_header_from_db (s, mir);

	if (success) {
		CamelIMAPXSummary *ims;
		gchar *part = mir->bdata;

		ims = CAMEL_IMAPX_SUMMARY (s);

		ims->version = bdata_extract_digit (&part);
		ims->validity = bdata_extract_digit (&part);

		if (ims->version >= 4) {
			ims->uidnext = bdata_extract_digit (&part);
			ims->modseq = bdata_extract_digit (&part);
		}

		if (ims->version > CAMEL_IMAPX_SUMMARY_VERSION) {
			g_warning ("Unknown summary version\n");
			errno = EINVAL;
			success = FALSE;
		}
	}

	return success;
}

static CamelFIRecord *
imapx_summary_summary_header_to_db (CamelFolderSummary *s,
                                    GError **error)
{
	struct _CamelFIRecord *fir;

	/* Chain up to parent's summary_header_to_db() method. */
	fir = CAMEL_FOLDER_SUMMARY_CLASS (
		camel_imapx_summary_parent_class)->
		summary_header_to_db (s, error);

	if (fir != NULL) {
		CamelIMAPXSummary *ims;

		ims = CAMEL_IMAPX_SUMMARY (s);

		fir->bdata = g_strdup_printf (
			"%d"
			" %" G_GUINT64_FORMAT
			" %" G_GUINT32_FORMAT
			" %" G_GUINT64_FORMAT,
			CAMEL_IMAPX_SUMMARY_VERSION,
			ims->validity,
			ims->uidnext,
			ims->modseq);
	}

	return fir;
}

static CamelMessageInfo *
imapx_summary_message_info_from_db (CamelFolderSummary *s,
                                    CamelMIRecord *mir)
{
	CamelMessageInfo *info;

	/* Chain up parent's message_info_from_db() method. */
	info = CAMEL_FOLDER_SUMMARY_CLASS (
		camel_imapx_summary_parent_class)->
		message_info_from_db (s, mir);

	if (info != NULL) {
		CamelIMAPXMessageInfo *imapx_info;
		gchar *part = mir->bdata;

		imapx_info = (CamelIMAPXMessageInfo *) info;
		imapx_info->server_flags = bdata_extract_digit (&part);
	}

	return info;
}

static CamelMIRecord *
imapx_summary_message_info_to_db (CamelFolderSummary *s,
                                  CamelMessageInfo *info)
{
	struct _CamelMIRecord *mir;

	/* Chain up to parent's message_info_to_db() method. */
	mir = CAMEL_FOLDER_SUMMARY_CLASS (
		camel_imapx_summary_parent_class)->
		message_info_to_db (s, info);

	if (mir != NULL) {
		CamelIMAPXMessageInfo *imapx_info;

		imapx_info = (CamelIMAPXMessageInfo *) info;
		mir->bdata = g_strdup_printf ("%u", imapx_info->server_flags);
	}

	return mir;
}

static CamelMessageContentInfo *
imapx_summary_content_info_from_db (CamelFolderSummary *summary,
                                    CamelMIRecord *mir)
{
	gchar *part = mir->cinfo;
	guint32 type = 0;

	if (part != NULL) {
		if (*part == ' ')
			part++;
		if (part != NULL)
			type = bdata_extract_digit (&part);
	}
	mir->cinfo = part;

	if (type) {
		/* Chain up to parent's content_info_from_db() method. */
		return CAMEL_FOLDER_SUMMARY_CLASS (
			camel_imapx_summary_parent_class)->
			content_info_from_db (summary, mir);
	} else {
		return camel_folder_summary_content_info_new (summary);
	}
}

static gboolean
imapx_summary_content_info_to_db (CamelFolderSummary *summary,
                                  CamelMessageContentInfo *info,
                                  CamelMIRecord *mir)
{
	gchar *oldr;

	if (info->type) {
		oldr = mir->cinfo;
		if (oldr != NULL)
			mir->cinfo = g_strdup_printf ("%s 1", oldr);
		else
			mir->cinfo = g_strdup ("1");
		g_free (oldr);

		/* Chain up to parent's content_info_to_db() method. */
		return CAMEL_FOLDER_SUMMARY_CLASS (
			camel_imapx_summary_parent_class)->
			content_info_to_db (summary, info, mir);

	} else {
		oldr = mir->cinfo;
		if (oldr != NULL)
			mir->cinfo = g_strdup_printf ("%s 0", oldr);
		else
			mir->cinfo = g_strdup ("0");
		g_free (oldr);

		return TRUE;
	}
}

static void
imapx_summary_message_info_free (CamelFolderSummary *summary,
                                 CamelMessageInfo *info)
{
	CamelIMAPXMessageInfo *imapx_info;

	imapx_info = (CamelIMAPXMessageInfo *) info;
	camel_flag_list_free (&imapx_info->server_user_flags);

	/* Chain up to parent's message_info_free() method. */
	CAMEL_FOLDER_SUMMARY_CLASS (camel_imapx_summary_parent_class)->
		message_info_free (summary, info);
}

static CamelMessageInfo *
imapx_summary_message_info_clone (CamelFolderSummary *summary,
                                  const CamelMessageInfo *info)
{
	CamelMessageInfo *copy;
	CamelIMAPXMessageInfo *imapx_copy;
	CamelIMAPXMessageInfo *imapx_info;

	/* Chain up to parent's message_info_clone() method. */
	copy = CAMEL_FOLDER_SUMMARY_CLASS (
		camel_imapx_summary_parent_class)->
		message_info_clone (summary, info);

	imapx_info = (CamelIMAPXMessageInfo *) info;
	imapx_copy = (CamelIMAPXMessageInfo *) copy;

	if (imapx_info->server_user_flags) {
		camel_flag_list_copy (
			&imapx_copy->server_user_flags,
			&imapx_info->server_user_flags);
	}

	imapx_copy->server_flags = imapx_info->server_flags;

	/* FIXME: parent clone should do this */
	imapx_copy->info.content =
		camel_folder_summary_content_info_new (summary);

	return copy;
}

static gboolean
imapx_summary_info_set_user_flag (CamelMessageInfo *info,
                                  const gchar *id,
                                  gboolean state)
{
	gboolean changed;

	/* Chain up to parent's info_set_user_flag() method. */
	changed = CAMEL_FOLDER_SUMMARY_CLASS (
		camel_imapx_summary_parent_class)->
		info_set_user_flag (info, id, state);

	/* there was a change, so do not forget to store it to server */
	if (changed) {
		CamelIMAPXMessageInfo *imapx_info;

		imapx_info = (CamelIMAPXMessageInfo *) info;
		imapx_info->info.flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
	}

	return changed;
}

static void
camel_imapx_summary_class_init (CamelIMAPXSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_size = sizeof (CamelIMAPXMessageInfo);
	folder_summary_class->content_info_size = sizeof (CamelIMAPXMessageContentInfo);
	folder_summary_class->summary_header_from_db = imapx_summary_summary_header_from_db;
	folder_summary_class->summary_header_to_db = imapx_summary_summary_header_to_db;
	folder_summary_class->message_info_from_db = imapx_summary_message_info_from_db;
	folder_summary_class->message_info_to_db = imapx_summary_message_info_to_db;
	folder_summary_class->content_info_from_db = imapx_summary_content_info_from_db;
	folder_summary_class->content_info_to_db = imapx_summary_content_info_to_db;
	folder_summary_class->message_info_free = imapx_summary_message_info_free;
	folder_summary_class->message_info_clone = imapx_summary_message_info_clone;
	folder_summary_class->info_set_user_flag = imapx_summary_info_set_user_flag;
}

static void
camel_imapx_summary_init (CamelIMAPXSummary *obj)
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

	return (a1 < a2) ? -1 : (a1 > a2) ? 1 : 0;
}

/**
 * camel_imapx_summary_new:
 * @folder: Parent folder.
 *
 * This will create a new CamelIMAPXSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Returns: A new CamelIMAPXSummary object.
 **/
CamelFolderSummary *
camel_imapx_summary_new (CamelFolder *folder)
{
	CamelStore *parent_store;
	CamelFolderSummary *summary;
	GError *local_error = NULL;

	parent_store = camel_folder_get_parent_store (folder);

	summary = g_object_new (CAMEL_TYPE_IMAPX_SUMMARY, "folder", folder, NULL);

	/* Don't do DB sort. Its pretty slow to load */
	if (folder && 0) {
		camel_db_set_collate (parent_store->cdb_r, "uid", "imapx_uid_sort", (CamelDBCollate) sort_uid_cmp);
		summary->sort_by = "uid";
		summary->collate = "imapx_uid_sort";
	}

	camel_folder_summary_set_build_content (summary, TRUE);

	if (!camel_folder_summary_load_from_db (summary, &local_error)) {
		/* FIXME: Isn't this dangerous ? We clear the summary
		if it cannot be loaded, for some random reason.
		We need to pass the error and find out why it is not loaded etc. ? */
		camel_folder_summary_clear (summary, NULL);
		g_message ("Unable to load summary: %s\n", local_error->message);
		g_clear_error (&local_error);
	}

	return summary;
}


/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2004 Novell Inc.
 *
 *  Authors:
 * 	parthasrathi susarla <sparthasrathi@novell.com>
 * Based on the IMAP summary class implementation by: 
 *    Michael Zucchi <notzed@ximian.com>
 *    Dan Winship <danw@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "camel-groupwise-summary.h"
#include "camel-file-utils.h"

#define CAMEL_GW_SUMMARY_VERSION (1)

/*Prototypes*/
static int gw_summary_header_load (CamelFolderSummary *, FILE *);
static int gw_summary_header_save (CamelFolderSummary *, FILE *);

static CamelMessageInfo *gw_message_info_load (CamelFolderSummary *s, FILE *in) ;

static int gw_message_info_save (CamelFolderSummary *s, FILE *out, CamelMessageInfo *info) ;
static CamelMessageContentInfo * gw_content_info_load (CamelFolderSummary *s, FILE *in) ;
static int gw_content_info_save (CamelFolderSummary *s, FILE *out, CamelMessageContentInfo *info) ;
		

static void camel_groupwise_summary_class_init (CamelGroupwiseSummaryClass *klass);
static void camel_groupwise_summary_init       (CamelGroupwiseSummary *obj);


/*End of Prototypes*/


static CamelFolderSummaryClass *camel_groupwise_summary_parent ;


CamelType
camel_groupwise_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(
				camel_folder_summary_get_type(), "CamelGroupwiseSummary",
				sizeof (CamelGroupwiseSummary),
				sizeof (CamelGroupwiseSummaryClass),
				(CamelObjectClassInitFunc) camel_groupwise_summary_class_init,
				NULL,
				(CamelObjectInitFunc) camel_groupwise_summary_init,
				NULL);
	}

	return type;
}

static CamelMessageInfo *
gw_message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
	CamelGroupwiseMessageInfo *to;
	const CamelGroupwiseMessageInfo *from = (const CamelGroupwiseMessageInfo *)mi;

	to = (CamelGroupwiseMessageInfo *)camel_groupwise_summary_parent->message_info_clone(s, mi);
	to->server_flags = from->server_flags;

	/* FIXME: parent clone should do this */
	to->info.content = camel_folder_summary_content_info_new(s);

	return (CamelMessageInfo *)to;
}

static void
camel_groupwise_summary_class_init (CamelGroupwiseSummaryClass *klass)
{
	CamelFolderSummaryClass *cfs_class = (CamelFolderSummaryClass *) klass;

	camel_groupwise_summary_parent = CAMEL_FOLDER_SUMMARY_CLASS (camel_type_get_global_classfuncs (camel_folder_summary_get_type()));

	cfs_class->message_info_clone = gw_message_info_clone ;
	cfs_class->summary_header_load = gw_summary_header_load;
	cfs_class->summary_header_save = gw_summary_header_save;
	cfs_class->message_info_load = gw_message_info_load;
	cfs_class->message_info_save = gw_message_info_save;
	cfs_class->content_info_load = gw_content_info_load;
	cfs_class->content_info_save = gw_content_info_save;
}


static void
camel_groupwise_summary_init (CamelGroupwiseSummary *obj)
{
	CamelFolderSummary *s = (CamelFolderSummary *)obj;

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelGroupwiseMessageInfo);
	s->content_info_size = sizeof(CamelGroupwiseMessageContentInfo);
}


/**
 * camel_groupwise_summary_new:
 * @filename: the file to store the summary in.
 *
 * This will create a new CamelGroupwiseSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Return value: A new CamelGroupwiseSummary object.
 **/
CamelFolderSummary *
camel_groupwise_summary_new (struct _CamelFolder *folder, const char *filename)
{
	CamelFolderSummary *summary = CAMEL_FOLDER_SUMMARY (
			camel_object_new (camel_groupwise_summary_get_type ()));
	
	summary->folder = folder ;
	camel_folder_summary_set_build_content (summary, TRUE);
	camel_folder_summary_set_filename (summary, filename);

	if (camel_folder_summary_load (summary) == -1) {
		camel_folder_summary_clear (summary);
		camel_folder_summary_touch (summary);
	}

	return summary;
}

static int
gw_summary_header_load (CamelFolderSummary *s, FILE *in)
{
	CamelGroupwiseSummary *ims = CAMEL_GROUPWISE_SUMMARY (s);

	if (camel_groupwise_summary_parent->summary_header_load (s, in) == -1)
		return -1 ;

	if (camel_file_util_decode_fixed_int32(in, &ims->version) == -1
			|| camel_file_util_decode_fixed_int32(in, &ims->validity) == -1)
		return -1;
	
	return 0 ;
}


static int
gw_summary_header_save (CamelFolderSummary *s, FILE *out)
{
	CamelGroupwiseSummary *ims = CAMEL_GROUPWISE_SUMMARY(s);

	if (camel_groupwise_summary_parent->summary_header_save (s, out) == -1)
		return -1;

	camel_file_util_encode_fixed_int32(out, CAMEL_GW_SUMMARY_VERSION);

	return camel_file_util_encode_fixed_int32(out, ims->validity);

}

static CamelMessageInfo *
gw_message_info_load (CamelFolderSummary *s, FILE *in)
{
	CamelMessageInfo *info ;
	CamelGroupwiseMessageInfo *gw_info ;


	info = camel_groupwise_summary_parent->message_info_load(s,in) ;
	if (info) {
		gw_info = (CamelGroupwiseMessageInfo*) info ;
		if (camel_file_util_decode_uint32 (in, &gw_info->server_flags) == -1)
			goto error ;
	}

	return info ;
error:
	//camel_folder_summary_info_free (s,in) ;
	camel_folder_info_free (in) ;
}


static int
gw_message_info_save (CamelFolderSummary *s, FILE *out, CamelMessageInfo *info)
{
	CamelGroupwiseMessageInfo *gw_info = (CamelGroupwiseMessageInfo *)info;

	if (camel_groupwise_summary_parent->message_info_save (s, out, info) == -1)
		return -1;

	return camel_file_util_encode_uint32 (out, gw_info->server_flags);
}


static CamelMessageContentInfo *
gw_content_info_load (CamelFolderSummary *s, FILE *in)
{       
	if (fgetc (in))
		return camel_groupwise_summary_parent->content_info_load (s, in);
	else
		return camel_folder_summary_content_info_new (s);
}


static int
gw_content_info_save (CamelFolderSummary *s, FILE *out,
		CamelMessageContentInfo *info)
{
	if (info->type) {
		fputc (1, out);
		return camel_groupwise_summary_parent->content_info_save (s, out, info);
	} else
		return fputc (0, out);
}



void
camel_gw_summary_add_offline (CamelFolderSummary *summary, const char *uid, CamelMimeMessage *message, const CamelMessageInfo *info)
{
	CamelGroupwiseMessageInfo *mi ; 
	const CamelFlag *flag ;
	const CamelTag *tag ;

	/* Create summary entry */
	mi = (CamelGroupwiseMessageInfo *)camel_folder_summary_info_new_from_message (summary, message) ;

	/* Copy flags 'n' tags */
	mi->info.flags = camel_message_info_flags(info) ;

	flag = camel_message_info_user_flags(info) ;
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
	mi->info.uid = g_strdup (uid);

	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);

}

void
camel_gw_summary_add_offline_uncached (CamelFolderSummary *summary, const char *uid, const CamelMessageInfo *info)
{
	CamelGroupwiseMessageInfo *mi;

	mi = camel_message_info_clone(info);
	mi->info.uid = g_strdup(uid);
	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);
}


/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors:
 * 	parthasrathi susarla <sparthasrathi@novell.com>
 * Based on the IMAP summary class implementation by: 
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
#include "camel-data-cache.h"
#include "camel-file-utils.h"
#include "camel-folder.h"
#include "camel-string-utils.h"

#include "camel-groupwise-folder.h"
#include "camel-groupwise-summary.h"

#define CAMEL_GW_SUMMARY_VERSION (1)

#define EXTRACT_FIRST_DIGIT(val) part ? val=strtoul (part, &part, 10) : 0;
#define EXTRACT_DIGIT(val) part++; part ? val=strtoul (part, &part, 10) : 0;

/*Prototypes*/
static int gw_summary_header_load (CamelFolderSummary *, FILE *);
static int gw_summary_header_save (CamelFolderSummary *, FILE *);

static CamelMessageInfo *gw_message_info_load (CamelFolderSummary *s, FILE *in) ;

static int gw_message_info_save (CamelFolderSummary *s, FILE *out, CamelMessageInfo *info) ;
static CamelMessageContentInfo * gw_content_info_load (CamelFolderSummary *s, FILE *in) ;
static int gw_content_info_save (CamelFolderSummary *s, FILE *out, CamelMessageContentInfo *info) ;
static gboolean gw_info_set_flags(CamelMessageInfo *info, guint32 flags, guint32 set);		

static int summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir);
static CamelFIRecord * summary_header_to_db (CamelFolderSummary *s, CamelException *ex);
static CamelMIRecord * message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info);
static CamelMessageInfo * message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);
static int content_info_to_db (CamelFolderSummary *s, CamelMessageContentInfo *info, CamelMIRecord *mir);
static CamelMessageContentInfo * content_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);

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
	cfs_class->info_set_flags = gw_info_set_flags;

	cfs_class->summary_header_to_db = summary_header_to_db;
	cfs_class->summary_header_from_db = summary_header_from_db;
	cfs_class->message_info_to_db = message_info_to_db;
	cfs_class->message_info_from_db = message_info_from_db;
	cfs_class->content_info_to_db = content_info_to_db;
	cfs_class->content_info_from_db = content_info_from_db;
	
}


static void
camel_groupwise_summary_init (CamelGroupwiseSummary *obj)
{
	CamelFolderSummary *s = (CamelFolderSummary *)obj;

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelGroupwiseMessageInfo);
	s->content_info_size = sizeof(CamelGroupwiseMessageContentInfo);
	
	/* Meta-summary - Overriding UID len */
	s->meta_summary->uid_len = 2048;
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
	CamelException ex;
	CamelFolderSummary *summary = CAMEL_FOLDER_SUMMARY (
			camel_object_new (camel_groupwise_summary_get_type ()));
	
	summary->folder = folder ;
	camel_folder_summary_set_build_content (summary, TRUE);
	camel_folder_summary_set_filename (summary, filename);

	camel_exception_init (&ex);
	if (camel_folder_summary_load_from_db (summary, &ex) == -1) {
		camel_folder_summary_clear (summary);
	}

	return summary;
}

static int
summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir)
{
	CamelGroupwiseSummary *gms = CAMEL_GROUPWISE_SUMMARY (s);
	char *part;

	if (camel_groupwise_summary_parent->summary_header_from_db (s, mir) == -1)
		return -1 ;

	part = mir->bdata;

	if (part)
		EXTRACT_FIRST_DIGIT(gms->version);

	if (part)
		EXTRACT_DIGIT (gms->validity);

	if (part && part++) {
		gms->time_string = g_strdup (part);
	}
		
	return 0;
}

static int
gw_summary_header_load (CamelFolderSummary *s, FILE *in)
{
	CamelGroupwiseSummary *gms = CAMEL_GROUPWISE_SUMMARY (s);

	if (camel_groupwise_summary_parent->summary_header_load (s, in) == -1)
		return -1 ;

	if (camel_file_util_decode_fixed_int32(in, &gms->version) == -1
			|| camel_file_util_decode_fixed_int32(in, &gms->validity) == -1)
		return -1;
	
	if (camel_file_util_decode_string (in, &gms->time_string) == -1)
		return -1;
	return 0 ;
}





static CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s, CamelException *ex)
{
	CamelGroupwiseSummary *ims = CAMEL_GROUPWISE_SUMMARY(s);
	struct _CamelFIRecord *fir;
	
	fir = camel_groupwise_summary_parent->summary_header_to_db (s, ex);
	if (!fir)
		return NULL;
	
	fir->bdata = g_strdup_printf ("%d %d %s", CAMEL_GW_SUMMARY_VERSION, ims->validity, ims->time_string);

	return fir;
	
}

static int
gw_summary_header_save (CamelFolderSummary *s, FILE *out)
{
	CamelGroupwiseSummary *gms = CAMEL_GROUPWISE_SUMMARY(s);

	if (camel_groupwise_summary_parent->summary_header_save (s, out) == -1)
		return -1;

	camel_file_util_encode_fixed_int32(out, CAMEL_GW_SUMMARY_VERSION);
	camel_file_util_encode_fixed_int32(out, gms->validity);
	return camel_file_util_encode_string (out, gms->time_string);
}

static CamelMessageInfo * 
message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	CamelMessageInfo *info;
	CamelGroupwiseMessageInfo *iinfo;

	info = camel_groupwise_summary_parent->message_info_from_db (s, mir);
	if (info) {
		char *part = mir->bdata;
		iinfo = (CamelGroupwiseMessageInfo *)info;
		EXTRACT_FIRST_DIGIT (iinfo->server_flags)
	}

	return info;}

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
	camel_message_info_free (info) ;
	return NULL ;
}

static CamelMIRecord * 
message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelGroupwiseMessageInfo *iinfo = (CamelGroupwiseMessageInfo *)info;
	struct _CamelMIRecord *mir;

	mir = camel_groupwise_summary_parent->message_info_to_db (s, info);
	if (mir) 
		mir->bdata = g_strdup_printf ("%u", iinfo->server_flags);

	return mir;	
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
content_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	char *part = mir->cinfo;
	guint32 type=0;
	
	if (part) {
		EXTRACT_FIRST_DIGIT (type);
	}
	if (type)
		return camel_groupwise_summary_parent->content_info_from_db (s, mir);
	else
		return camel_folder_summary_content_info_new (s);
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
content_info_to_db (CamelFolderSummary *s, CamelMessageContentInfo *info, CamelMIRecord *mir)
{

	if (info->type) {
		mir->cinfo = g_strdup ("1");
		return camel_groupwise_summary_parent->content_info_to_db (s, info, mir);
	} else {
		mir->cinfo = g_strdup ("0");
		return 0;
	}
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

static gboolean
gw_info_set_flags (CamelMessageInfo *info, guint32 flags, guint32 set)
{
	guint32 old;
	CamelMessageInfoBase *mi = (CamelMessageInfoBase *)info;

	/* TODO: locking? */

	old = mi->flags;
	/* we don't set flags which aren't appropriate for the folder*/
	if ((set == (CAMEL_MESSAGE_JUNK|CAMEL_MESSAGE_JUNK_LEARN|CAMEL_MESSAGE_SEEN)) && (old & CAMEL_GW_MESSAGE_JUNK))
		return FALSE;
	
	mi->flags = (old & ~flags) | (set & flags);
	if (old != mi->flags) {
		mi->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;

		if (mi->summary) {

				if ((set & CAMEL_MESSAGE_SEEN) && !(old & CAMEL_MESSAGE_SEEN)) {
						mi->summary->unread_count -- ;
				} else if ( (!(set & CAMEL_MESSAGE_SEEN)) && (old & CAMEL_MESSAGE_SEEN) ) {
						mi->summary->unread_count ++ ;
				}

				if ((flags & CAMEL_MESSAGE_DELETED) && !(old & CAMEL_MESSAGE_DELETED)) {
						mi->summary->deleted_count ++ ;

						#warning "What to do when the user has set to show-deleted-messages "
						mi->summary->visible_count -- ;

						if (!(flags & CAMEL_MESSAGE_SEEN))
							mi->summary->unread_count -- ;
				}

				camel_folder_summary_touch(mi->summary);
		}
	}
	/* This is a hack, we are using CAMEL_MESSAGE_JUNK justo to hide the item
	 * we make sure this doesn't have any side effects*/
	
	if ((set == CAMEL_MESSAGE_JUNK_LEARN) && (old & CAMEL_GW_MESSAGE_JUNK)) {
		mi->flags |= CAMEL_GW_MESSAGE_NOJUNK | CAMEL_MESSAGE_JUNK;

		/* This has ugly side-effects. Evo will never learn unjunk. 

		   We need to create one CAMEL_MESSAGE_HIDDEN flag which must be used for all hiding operations. We must also get rid of the seperate file that is maintained somewhere in evolution/mail/em-folder-browser.c for hidden messages
		 */

		if (mi->summary) {
			camel_folder_summary_touch(mi->summary);
		}

	} else	if ((old & ~CAMEL_MESSAGE_SYSTEM_MASK) == (mi->flags & ~CAMEL_MESSAGE_SYSTEM_MASK)) 
		return FALSE;

	if (mi->summary && mi->summary->folder && mi->uid) {
		CamelFolderChangeInfo *changes = camel_folder_change_info_new();

		camel_folder_change_info_change_uid(changes, camel_message_info_uid(info));
		camel_object_trigger_event(mi->summary->folder, "folder_changed", changes);
		camel_folder_change_info_free(changes);
	}

	return TRUE;

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
	mi->info.uid = camel_pstring_strdup (uid);

	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);

}

void
camel_gw_summary_add_offline_uncached (CamelFolderSummary *summary, const char *uid, const CamelMessageInfo *info)
{
	CamelGroupwiseMessageInfo *mi;

	mi = camel_message_info_clone(info);
	mi->info.uid = camel_pstring_strdup(uid);
	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);
}

void
groupwise_summary_clear (CamelFolderSummary *summary, gboolean uncache)
{
	CamelFolderChangeInfo *changes;
	CamelMessageInfo *info;
	int i, count;
	const char *uid;

	changes = camel_folder_change_info_new ();
	count = camel_folder_summary_count (summary);
	for (i = 0; i < count; i++) {
		if (!(info = camel_folder_summary_index (summary, i)))
			continue;
  		
		uid = camel_message_info_uid (info);
		camel_folder_change_info_remove_uid (changes, uid);
		camel_folder_summary_remove_uid (summary, uid);
		camel_message_info_free(info);
	}

	camel_folder_summary_clear (summary);
	//camel_folder_summary_save (summary);

	if (uncache)
		camel_data_cache_clear (((CamelGroupwiseFolder *) summary->folder)->cache, "cache", NULL);

	if (camel_folder_change_info_changed (changes))
		camel_object_trigger_event (summary->folder, "folder_changed", changes);
	camel_folder_change_info_free (changes);
}


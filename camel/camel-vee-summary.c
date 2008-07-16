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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "camel-folder.h"
#include "camel-store.h"
#include "camel-vee-summary.h"
#include "camel-private.h"
#include "camel-string-utils.h"

#define d(x)

static CamelFolderSummaryClass *camel_vee_summary_parent;

static void
vee_message_info_free(CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)info;

	g_free(info->uid);
	camel_object_unref (mi->summary);
}

static CamelMessageInfo *
vee_message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
	CamelVeeMessageInfo *to;
	const CamelVeeMessageInfo *from = (const CamelVeeMessageInfo *)mi;

	to = (CamelVeeMessageInfo *)camel_message_info_new(s);

	to->summary = from->summary;
	/* FIXME: We may not need this during CamelDBSummary */
	camel_object_ref (to->summary);
	to->info.summary = s;
	
	return (CamelMessageInfo *)to;
}

static const void *
vee_info_ptr (const CamelMessageInfo *mi, int id)
{
	CamelVeeMessageInfo *vmi = (CamelVeeMessageInfo *) mi;
	CamelMessageInfo *info;
	gpointer p;
	
	info = camel_folder_summary_uid (vmi->summary, mi->uid+8);
	p = (gpointer) camel_message_info_ptr(info, id);
	camel_message_info_free (info);

	return p;
}

static guint32
vee_info_uint32(const CamelMessageInfo *mi, int id)
{
	CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
	guint32 ret = camel_message_info_uint32 (rmi, id);

	camel_message_info_free (rmi);

	return ret;

}

static time_t
vee_info_time(const CamelMessageInfo *mi, int id)
{
	CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
	time_t ret = camel_message_info_time (rmi, id);
	camel_message_info_free (rmi);

	return ret;
}

static gboolean
vee_info_user_flag(const CamelMessageInfo *mi, const char *id)
{
	CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
	gboolean ret = 	camel_message_info_user_flag (rmi, id);
	camel_message_info_free (rmi);

	return ret;
}

static const char *
vee_info_user_tag(const CamelMessageInfo *mi, const char *id)
{
	CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
	const char *ret = camel_message_info_user_tag (rmi, id);
	camel_message_info_free (rmi);

	return ret;
}

static gboolean
vee_info_set_user_flag(CamelMessageInfo *mi, const char *name, gboolean value)
{
	int res = FALSE;

	if (mi->uid) {
		CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
		res = camel_message_info_set_user_flag(rmi, name, value);
		camel_message_info_free (rmi);		
	}
 
	return res;
}

static gboolean
vee_info_set_user_tag(CamelMessageInfo *mi, const char *name, const char *value)
{
	int res = FALSE;

	if (mi->uid) {
		CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
		res = camel_message_info_set_user_tag(rmi, name, value);
		camel_message_info_free (rmi);			
	}
 
	return res;
}

static gboolean
vee_info_set_flags(CamelMessageInfo *mi, guint32 flags, guint32 set)
{
	int res = FALSE;

	if (mi->uid) {
		CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);		
		res = camel_message_info_set_flags(rmi, flags, set);
		camel_message_info_free (rmi);
	}
 
	return res;
}

static CamelMessageInfo *
message_info_from_uid (CamelFolderSummary *s, const char *uid)
{
	CamelMessageInfoBase *info;

	#warning "too bad design. Need to peek it from cfs instead of hacking ugly like this"
	CAMEL_SUMMARY_LOCK(s, summary_lock);
	CAMEL_SUMMARY_LOCK(s, ref_lock);

	info = g_hash_table_lookup (s->loaded_infos, uid);

	if (info)
		info->refcount++;

	CAMEL_SUMMARY_UNLOCK(s, ref_lock);
	CAMEL_SUMMARY_UNLOCK(s, summary_lock);

	return (CamelMessageInfo *) info;	
}

static void
camel_vee_summary_class_init (CamelVeeSummaryClass *klass)
{
	((CamelFolderSummaryClass *)klass)->message_info_clone = vee_message_info_clone;
	((CamelFolderSummaryClass *)klass)->message_info_free = vee_message_info_free;

	((CamelFolderSummaryClass *)klass)->info_ptr = vee_info_ptr;
	((CamelFolderSummaryClass *)klass)->info_uint32 = vee_info_uint32;
	((CamelFolderSummaryClass *)klass)->info_time = vee_info_time;
	((CamelFolderSummaryClass *)klass)->info_user_flag = vee_info_user_flag;
	((CamelFolderSummaryClass *)klass)->info_user_tag = vee_info_user_tag;

#if 0
	((CamelFolderSummaryClass *)klass)->info_set_string = vee_info_set_string;
	((CamelFolderSummaryClass *)klass)->info_set_uint32 = vee_info_set_uint32;
	((CamelFolderSummaryClass *)klass)->info_set_time = vee_info_set_time;
	((CamelFolderSummaryClass *)klass)->info_set_references = vee_info_set_references;
#endif
	((CamelFolderSummaryClass *)klass)->info_set_user_flag = vee_info_set_user_flag;
	((CamelFolderSummaryClass *)klass)->info_set_user_tag = vee_info_set_user_tag;

	((CamelFolderSummaryClass *)klass)->info_set_flags = vee_info_set_flags;
	((CamelFolderSummaryClass *)klass)->message_info_from_uid = message_info_from_uid;
}

static void
camel_vee_summary_init (CamelVeeSummary *obj)
{
	CamelFolderSummary *s = (CamelFolderSummary *)obj;

	s->message_info_size = sizeof(CamelVeeMessageInfo);
	s->content_info_size = 0;
}

CamelType
camel_vee_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		camel_vee_summary_parent = (CamelFolderSummaryClass *)camel_folder_summary_get_type();

		type = camel_type_register(
			camel_folder_summary_get_type(), "CamelVeeSummary",
			sizeof (CamelVeeSummary),
			sizeof (CamelVeeSummaryClass),
			(CamelObjectClassInitFunc) camel_vee_summary_class_init,
			NULL,
			(CamelObjectInitFunc) camel_vee_summary_init,
			NULL);
	}

	return type;
}

/**
 * camel_vee_summary_new:
 * @parent: Folder its attached to.
 *
 * This will create a new CamelVeeSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Return value: A new CamelVeeSummary object.
 **/
CamelFolderSummary *
camel_vee_summary_new(CamelFolder *parent)
{
	CamelVeeSummary *s;

	s = (CamelVeeSummary *)camel_object_new(camel_vee_summary_get_type());
	s->summary.folder = parent;

        #warning "fix exceptions and note return values"
	#warning "if Evo's junk/trash vfolders make it VJunk VTrash instead of .#evolution/Junk-or-whatever"		
	camel_db_create_vfolder (parent->cdb, parent->full_name, NULL);

	#warning "handle excep and ret"
	camel_folder_summary_header_load_from_db ((CamelFolderSummary *)s, parent->parent_store, parent->full_name, NULL);	
	return &s->summary;
}

GPtrArray *
camel_vee_summary_get_ids (CamelVeeSummary *summary, char hash[8])
{
	char *shash = g_strdup_printf("%c%c%c%c%c%c%c%c", hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7]);
	CamelFolderSummary *cfs = (CamelFolderSummary *)summary;
	GPtrArray *array;

	#warning "fix exception passing"
	array = camel_db_get_vuids_from_vfolder(cfs->folder->cdb, cfs->folder->full_name, shash, NULL);
	
	g_free(shash);

	return array;
}

CamelVeeMessageInfo *
camel_vee_summary_add(CamelVeeSummary *s, CamelFolderSummary *summary, const char *uid, const char hash[8])
{
	CamelVeeMessageInfo *mi;
	char *vuid;

	vuid = g_malloc(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);
	
	#warning do we need it really ?
/* 	mi = (CamelVeeMessageInfo *)camel_folder_summary_uid(&s->summary, vuid); */
/* 	if (mi) { */
/* 		d(printf("w:clash, we already have '%s' in summary\n", vuid)); */
/* 		camel_message_info_free((CamelMessageInfo *)mi); */
/* 		g_free(vuid); */
/* 		return NULL; */
/* 	} */

	mi = (CamelVeeMessageInfo *) message_info_from_uid(&s->summary, vuid); 
	if (mi) {
		g_warning ("%s - already there\n", vuid);
		g_free (vuid);
		return mi;
	}

	mi = (CamelVeeMessageInfo *)camel_message_info_new(&s->summary);
	mi->summary = summary;
	camel_object_ref (summary);
	mi->info.uid = (char *) camel_pstring_strdup (vuid);
	g_free (vuid);
	camel_message_info_ref (mi);
	camel_folder_summary_insert(&s->summary, (CamelMessageInfo *)mi, FALSE);
	
	return mi;
}

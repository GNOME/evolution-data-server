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

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-folder.h"
#include "camel-store.h"
#include "camel-vee-summary.h"
#include "camel-vee-folder.h"
#include "camel-vee-store.h"
#include "camel-string-utils.h"

#define d(x)

static const gchar *unread_str = " (and\n  \n     (match-all (not (system-flag  \"Seen\")))\n    \n  )\n;  (or\n  \n     (match-all (not (system-flag  \"Seen\")))\n    \n  )\n; (match-threads \"all\"  (and\n  \n     (match-all (not (system-flag  \"Seen\")))\n    \n  )\n)\n;  (match-threads \"all\"  (or\n  \n     (match-all (not (system-flag  \"Seen\")))\n    \n  )\n)\n;";

G_DEFINE_TYPE (CamelVeeSummary, camel_vee_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static void
vee_message_info_free(CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)info;

	camel_pstring_free(info->uid);
	g_object_unref (mi->summary);
}

static CamelMessageInfo *
vee_message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
	CamelVeeMessageInfo *to;
	const CamelVeeMessageInfo *from = (const CamelVeeMessageInfo *)mi;

	to = (CamelVeeMessageInfo *)camel_message_info_new(s);

	to->summary = g_object_ref (from->summary);
	to->info.summary = s;
	to->info.uid = camel_pstring_strdup(from->info.uid);

	return (CamelMessageInfo *)to;
}

#define HANDLE_NULL_INFO(value) if (!rmi) { d(g_warning (G_STRLOC ": real info is NULL for %s, safeguarding\n", mi->uid)); return value; }

static gconstpointer
vee_info_ptr (const CamelMessageInfo *mi, gint id)
{
	CamelVeeMessageInfo *vmi = (CamelVeeMessageInfo *) mi;
	CamelMessageInfo *rmi;
	gpointer p;

	rmi = camel_folder_summary_uid (vmi->summary, mi->uid+8);
	HANDLE_NULL_INFO(NULL);
	p = (gpointer) camel_message_info_ptr(rmi, id);
	camel_message_info_free (rmi);

	return p;
}

static guint32
vee_info_uint32(const CamelMessageInfo *mi, gint id)
{
	CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
	guint32 ret;

	HANDLE_NULL_INFO(0);
	ret = camel_message_info_uint32 (rmi, id);
	camel_message_info_free (rmi);

	return ret;

}

static time_t
vee_info_time(const CamelMessageInfo *mi, gint id)
{
	CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
	time_t ret;

	HANDLE_NULL_INFO(0);
	ret = camel_message_info_time (rmi, id);
	camel_message_info_free (rmi);

	return ret;
}

static gboolean
vee_info_user_flag(const CamelMessageInfo *mi, const gchar *id)
{
	CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
	gboolean ret;

	HANDLE_NULL_INFO(FALSE);
	ret =	camel_message_info_user_flag (rmi, id);
	camel_message_info_free (rmi);

	return ret;
}

static const gchar *
vee_info_user_tag(const CamelMessageInfo *mi, const gchar *id)
{
	CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
	const gchar *ret;

	HANDLE_NULL_INFO("");
	ret = camel_message_info_user_tag (rmi, id);
	camel_message_info_free (rmi);

	return ret;
}

static gboolean
vee_info_set_user_flag(CamelMessageInfo *mi, const gchar *name, gboolean value)
{
	gint res = FALSE;
	gboolean hacked_unread_folder = FALSE;
	CamelVeeFolder *vf = (CamelVeeFolder *)mi->summary->folder;

	if (camel_debug("vfolderexp"))
		printf (
			"Expression for vfolder '%s' is '%s'\n",
			camel_folder_get_full_name (mi->summary->folder),
			g_strescape (vf->expression, ""));

	if (camel_vee_folder_get_unread_vfolder (vf) == -1)
		camel_vee_summary_load_check_unread_vfolder (CAMEL_VEE_SUMMARY (mi->summary));

	if (camel_vee_folder_get_unread_vfolder (vf) == 1)
		hacked_unread_folder = TRUE;

	if (mi->uid) {
		CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
		HANDLE_NULL_INFO(FALSE);

		if (hacked_unread_folder)
			camel_vee_folder_mask_event_folder_changed ((CamelVeeFolder *)mi->summary->folder, rmi->summary->folder);

		res = camel_message_info_set_user_flag(rmi, name, value);

		if (hacked_unread_folder)
			camel_vee_folder_unmask_event_folder_changed ((CamelVeeFolder *)mi->summary->folder, rmi->summary->folder);

		camel_message_info_free (rmi);
	}

	return res;
}

static gboolean
vee_info_set_user_tag(CamelMessageInfo *mi, const gchar *name, const gchar *value)
{
	gint res = FALSE;

	if (mi->uid) {
		CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
		HANDLE_NULL_INFO(FALSE);
		res = camel_message_info_set_user_tag(rmi, name, value);
		camel_message_info_free (rmi);
	}

	return res;
}

/**
 * camel_vee_summary_load_check_unread_vfolder:
 *
 * Since: 2.26
 **/
void
camel_vee_summary_load_check_unread_vfolder (CamelVeeSummary *vs)
{
	static gint only_once = FALSE;
	static gchar *exp = NULL;
	gboolean hacked_unread_folder = FALSE;
	CamelVeeFolder *vf;

	g_return_if_fail (vs != NULL);

	vf = (CamelVeeFolder *) ((CamelFolderSummary *)vs)->folder;

	/* HACK: Ugliest of all hacks. Its virtually not possible now
	 * to maintain counts and the non matching uids of unread vfolder here.
	 * So, I hardcode unread vfolder expression and hack it. */
	if (!only_once) {
		exp =  g_getenv("CAMEL_VFOLDER_UNREAD_EXP") ? g_strcompress(g_getenv("CAMEL_VFOLDER_UNREAD_EXP")) : NULL;
		only_once = TRUE;
	}

	if (!exp || !*exp)
		exp = g_strcompress(unread_str);

	if (vf->expression && strstr(exp, vf->expression) &&  (vf->flags & CAMEL_STORE_VEE_FOLDER_SPECIAL) == 0)
		hacked_unread_folder = TRUE;

	if (hacked_unread_folder)
		camel_vee_folder_set_unread_vfolder (vf, 1);
	else
		camel_vee_folder_set_unread_vfolder (vf, 0);
}

static gboolean
vee_info_set_flags(CamelMessageInfo *mi, guint32 flags, guint32 set)
{
	gint res = FALSE;
	CamelVeeFolder *vf = (CamelVeeFolder *)mi->summary->folder;
	gboolean hacked_unread_folder = FALSE;

	if (camel_debug("vfolderexp"))
		printf (
			"Expression for vfolder '%s' is '%s'\n",
			camel_folder_get_full_name (mi->summary->folder),
			g_strescape (vf->expression, ""));

	if (camel_vee_folder_get_unread_vfolder (vf) == -1)
		camel_vee_summary_load_check_unread_vfolder (CAMEL_VEE_SUMMARY (mi->summary));

	if (camel_vee_folder_get_unread_vfolder (vf) == 1)
		hacked_unread_folder = TRUE;

	if (mi->uid) {
		guint32 old_visible, visible, old_unread;
		CamelMessageInfo *rmi = camel_folder_summary_uid (((CamelVeeMessageInfo *)mi)->summary, mi->uid+8);
		CamelVeeSummary *vsummary = (CamelVeeSummary *)mi->summary;

		HANDLE_NULL_INFO(FALSE);

		old_visible = rmi->summary->visible_count;
		old_unread = mi->summary->unread_count;
		camel_folder_summary_update_counts_by_flags (mi->summary, camel_message_info_flags (rmi), TRUE);

		if (hacked_unread_folder)
			camel_vee_folder_mask_event_folder_changed ((CamelVeeFolder *)mi->summary->folder, rmi->summary->folder);

		camel_folder_freeze(rmi->summary->folder);
		res = camel_message_info_set_flags(rmi, flags, set);
		((CamelVeeMessageInfo *) mi)->old_flags = camel_message_info_flags (rmi);
		camel_folder_thaw(rmi->summary->folder);

		if (hacked_unread_folder)
			camel_vee_folder_unmask_event_folder_changed ((CamelVeeFolder *)mi->summary->folder, rmi->summary->folder);

		visible = rmi->summary->visible_count;

		/* Keep the summary in sync */
		camel_folder_summary_update_counts_by_flags (mi->summary, camel_message_info_flags (rmi), FALSE);

		if (hacked_unread_folder && !vsummary->fake_visible_count)
			vsummary->fake_visible_count = mi->summary->visible_count;

		if (vsummary->fake_visible_count || hacked_unread_folder)
			vsummary->fake_visible_count += visible - old_visible;

		d(printf("VF %d %d %d %d %d\n", mi->summary->unread_count, mi->summary->deleted_count, mi->summary->junk_count, mi->summary->junk_not_deleted_count, mi->summary->visible_count));

		/* This is where the ugly-created-hack is used */
		if (hacked_unread_folder && mi->summary->unread_count - old_unread != 0) {
			CamelFolderChangeInfo *changes = camel_folder_change_info_new();
			GPtrArray *match, *array;

			camel_folder_change_info_change_uid(changes, mi->uid);

			array = g_ptr_array_new ();
			g_ptr_array_add (array, (gpointer)rmi->uid);

			match = camel_folder_search_by_uids (rmi->summary->folder, vf->expression, array, NULL);
			if ((match && !match->len) || !match) {
				vsummary->fake_visible_count--;
			} else {
				vsummary->fake_visible_count++;
			}

			g_ptr_array_free (array, TRUE);
			if (match)
				camel_folder_search_free(rmi->summary->folder, match);

			camel_folder_changed (mi->summary->folder, changes);
			camel_folder_change_info_free(changes);
		}
		camel_message_info_free (rmi);
	}

	return res;
}

static CamelMessageInfo *
message_info_from_uid (CamelFolderSummary *s, const gchar *uid)
{
	CamelMessageInfo *info;

	/* FIXME[disk-summary] too bad design. Need to peek it from cfs
	 * instead of hacking ugly like this */
	camel_folder_summary_lock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	info = g_hash_table_lookup (s->loaded_infos, uid);

	if (info)
		camel_message_info_ref (info);

	camel_folder_summary_unlock (s, CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	if (!info) {
		CamelVeeMessageInfo *vinfo;
		gchar tmphash[9];

		/* This function isn't really nice. But no great way
		 * But in vfolder case, this may not be so bad, as vuid has the hash in first 8 bytes.
		 * So this just compares the entire string only if it belongs to the same folder.
		 * Otherwise, the first byte itself would return in strcmp, saving the CPU.
		 */
		if (!camel_folder_summary_check_uid (s, uid)) {
			d(g_message ("Unable to find %s in the summary of %s", uid, s->folder->full_name));
			return NULL;
		}

		/* Create the info and load it, its so easy. */
		info = camel_message_info_new (s);
		camel_message_info_ref(info);
		info->dirty = FALSE;
		vinfo = (CamelVeeMessageInfo *) info;
		info->uid = camel_pstring_strdup(uid);
		strncpy(tmphash, uid, 8);
		tmphash[8] = 0;
		vinfo->summary = g_hash_table_lookup(((CamelVeeFolder *) s->folder)->hashes, tmphash);
		g_object_ref (vinfo->summary);
		camel_folder_summary_insert (s, info, FALSE);
	}
	return info;
}

static void
camel_vee_summary_class_init (CamelVeeSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_size = sizeof (CamelVeeMessageInfo);
	folder_summary_class->content_info_size = 0;
	folder_summary_class->message_info_clone = vee_message_info_clone;
	folder_summary_class->message_info_free = vee_message_info_free;
	folder_summary_class->info_ptr = vee_info_ptr;
	folder_summary_class->info_uint32 = vee_info_uint32;
	folder_summary_class->info_time = vee_info_time;
	folder_summary_class->info_user_flag = vee_info_user_flag;
	folder_summary_class->info_user_tag = vee_info_user_tag;
	folder_summary_class->info_set_user_flag = vee_info_set_user_flag;
	folder_summary_class->info_set_user_tag = vee_info_set_user_tag;
	folder_summary_class->info_set_flags = vee_info_set_flags;
	folder_summary_class->message_info_from_uid = message_info_from_uid;
}

static void
camel_vee_summary_init (CamelVeeSummary *vee_summary)
{
}

/**
 * camel_vee_summary_new:
 * @parent: Folder its attached to.
 *
 * This will create a new CamelVeeSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Returns: A new CamelVeeSummary object.
 **/
CamelFolderSummary *
camel_vee_summary_new(CamelFolder *parent)
{
	CamelVeeSummary *s;
	CamelStore *parent_store;
	const gchar *full_name;

	s = g_object_new (CAMEL_TYPE_VEE_SUMMARY, NULL);
	s->summary.folder = parent;
	s->force_counts = FALSE;
	s->fake_visible_count = 0;

        /* FIXME[disk-summary] fix exceptions and note return values */
	/* FIXME[disk-summary] if Evo's junk/trash vfolders make it VJunk
	 * VTrash instead of .#evolution/Junk-or-whatever */
	full_name = camel_folder_get_full_name (parent);
	parent_store = camel_folder_get_parent_store (parent);
	camel_db_create_vfolder (parent_store->cdb_w, full_name, NULL);

	return &s->summary;
}

/**
 * camel_vee_summary_get_ids:
 *
 * Since: 2.24
 **/
GPtrArray *
camel_vee_summary_get_ids (CamelVeeSummary *summary, gchar hash[8])
{
	gchar *shash = g_strdup_printf("%c%c%c%c%c%c%c%c", hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7]);
	CamelFolderSummary *cfs = (CamelFolderSummary *)summary;
	CamelStore *parent_store;
	GPtrArray *array;
	const gchar *full_name;

	/* FIXME[disk-summary] fix exception passing */
	full_name = camel_folder_get_full_name (cfs->folder);
	parent_store = camel_folder_get_parent_store (cfs->folder);
	array = camel_db_get_vuids_from_vfolder (parent_store->cdb_r, full_name, shash, NULL);

	g_free(shash);

	return array;
}

CamelVeeMessageInfo *
camel_vee_summary_add(CamelVeeSummary *s, CamelFolderSummary *summary, const gchar *uid, const gchar hash[8])
{
	CamelVeeMessageInfo *mi;
	CamelMessageInfo *rmi;
	gchar *vuid;
	vuid = g_malloc(strlen(uid)+9);
	memcpy(vuid, hash, 8);
	strcpy(vuid+8, uid);

	camel_folder_summary_lock (CAMEL_FOLDER_SUMMARY (s), CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);
	mi = (CamelVeeMessageInfo *) g_hash_table_lookup(((CamelFolderSummary *) s)->loaded_infos, vuid);
	camel_folder_summary_unlock (CAMEL_FOLDER_SUMMARY (s), CAMEL_FOLDER_SUMMARY_SUMMARY_LOCK);

	if (mi) {
		/* Possible that the entry is loaded, see if it has the summary */
		d(g_message ("%s - already there\n", vuid));
		g_free (vuid);
		if (!mi->summary)
			mi->summary = g_object_ref (summary);

		camel_message_info_ref (mi);
		return mi;
	}

	mi = (CamelVeeMessageInfo *)camel_message_info_new(&s->summary);
	mi->summary = g_object_ref (summary);
	mi->info.uid = (gchar *) camel_pstring_strdup (vuid);
	g_free (vuid);
	camel_message_info_ref (mi);

	/* Get actual flags and store it */
	rmi = camel_folder_summary_uid (summary, uid);
	if (rmi) {
		mi->old_flags = camel_message_info_flags (rmi);
		camel_message_info_free (rmi);
	}

	camel_folder_summary_insert(&s->summary, (CamelMessageInfo *)mi, FALSE);
	camel_folder_summary_update_counts_by_flags (&s->summary, camel_message_info_flags (mi), FALSE);

	return mi;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-folder.h"
#include "camel-store.h"
#include "camel-vee-summary.h"
#include "camel-vee-folder.h"
#include "camel-vee-store.h"
#include "camel-vtrash-folder.h"
#include "camel-string-utils.h"

#define d(x)

#define CAMEL_VEE_SUMMARY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_VEE_SUMMARY, CamelVeeSummaryPrivate))

struct _CamelVeeSummaryPrivate {
	/* CamelFolder * => GHashTable * of gchar *vuid */
	GHashTable *vuids_by_subfolder;
};

G_DEFINE_TYPE (CamelVeeSummary, camel_vee_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static void
vee_message_info_free (CamelFolderSummary *s,
                       CamelMessageInfo *info)
{
	CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *) info;

	g_object_unref (mi->orig_summary);

	CAMEL_FOLDER_SUMMARY_CLASS (camel_vee_summary_parent_class)->message_info_free (s, info);
}

static CamelMessageInfo *
vee_message_info_clone (CamelFolderSummary *s,
                        const CamelMessageInfo *mi)
{
	CamelVeeMessageInfo *to;
	const CamelVeeMessageInfo *from = (const CamelVeeMessageInfo *) mi;

	to = (CamelVeeMessageInfo *) camel_message_info_new (s);

	to->orig_summary = g_object_ref (from->orig_summary);
	to->info.summary = s;
	to->info.uid = camel_pstring_strdup (from->info.uid);

	return (CamelMessageInfo *) to;
}

#define HANDLE_NULL_INFO(value) if (!rmi) { d(g_warning (G_STRLOC ": real info is NULL for %s, safeguarding\n", mi->uid)); return value; }

static gconstpointer
vee_info_ptr (const CamelMessageInfo *mi,
              gint id)
{
	CamelVeeMessageInfo *vmi = (CamelVeeMessageInfo *) mi;
	CamelMessageInfo *rmi;
	gpointer p;

	rmi = camel_folder_summary_get (vmi->orig_summary, mi->uid + 8);
	HANDLE_NULL_INFO (NULL);
	p = (gpointer) camel_message_info_ptr (rmi, id);
	camel_message_info_unref (rmi);

	return p;
}

static guint32
vee_info_uint32 (const CamelMessageInfo *mi,
                 gint id)
{
	CamelMessageInfo *rmi = camel_folder_summary_get (((CamelVeeMessageInfo *) mi)->orig_summary, mi->uid + 8);
	guint32 ret;

	HANDLE_NULL_INFO (0);
	ret = camel_message_info_uint32 (rmi, id);
	camel_message_info_unref (rmi);

	return ret;

}

static time_t
vee_info_time (const CamelMessageInfo *mi,
               gint id)
{
	CamelMessageInfo *rmi = camel_folder_summary_get (((CamelVeeMessageInfo *) mi)->orig_summary, mi->uid + 8);
	time_t ret;

	HANDLE_NULL_INFO (0);
	ret = camel_message_info_time (rmi, id);
	camel_message_info_unref (rmi);

	return ret;
}

static gboolean
vee_info_user_flag (const CamelMessageInfo *mi,
                    const gchar *id)
{
	CamelMessageInfo *rmi = camel_folder_summary_get (((CamelVeeMessageInfo *) mi)->orig_summary, mi->uid + 8);
	gboolean ret;

	HANDLE_NULL_INFO (FALSE);
	ret = camel_message_info_user_flag (rmi, id);
	camel_message_info_unref (rmi);

	return ret;
}

static const gchar *
vee_info_user_tag (const CamelMessageInfo *mi,
                   const gchar *id)
{
	CamelMessageInfo *rmi = camel_folder_summary_get (((CamelVeeMessageInfo *) mi)->orig_summary, mi->uid + 8);
	const gchar *ret;

	HANDLE_NULL_INFO ("");
	ret = camel_message_info_user_tag (rmi, id);
	camel_message_info_unref (rmi);

	return ret;
}

static void
vee_summary_notify_mi_changed (CamelVeeFolder *vfolder,
                               CamelMessageInfo *mi)
{
	CamelFolderChangeInfo *changes;

	g_return_if_fail (vfolder != NULL);
	g_return_if_fail (mi != NULL);

	changes = camel_folder_change_info_new ();

	camel_folder_change_info_change_uid (changes, camel_message_info_uid (mi));
	camel_folder_changed (CAMEL_FOLDER (vfolder), changes);
	camel_folder_change_info_free (changes);
}

static gboolean
vee_info_set_user_flag (CamelMessageInfo *mi,
                        const gchar *name,
                        gboolean value)
{
	gint res = FALSE;
	CamelVeeFolder *vf = (CamelVeeFolder *) camel_folder_summary_get_folder (mi->summary);

	if (mi->uid) {
		CamelMessageInfo *rmi = camel_folder_summary_get (((CamelVeeMessageInfo *) mi)->orig_summary, mi->uid + 8);
		gboolean ignore_changes = !CAMEL_IS_VTRASH_FOLDER (vf);

		HANDLE_NULL_INFO (FALSE);

		/* ignore changes done in the folder itself,
		 * unless it's a vTrash or vJunk folder */
		if (ignore_changes)
			camel_vee_folder_ignore_next_changed_event (vf, camel_folder_summary_get_folder (rmi->summary));

		res = camel_message_info_set_user_flag (rmi, name, value);

		if (ignore_changes) {
			if (res)
				vee_summary_notify_mi_changed (vf, mi);
			else
				camel_vee_folder_remove_from_ignore_changed_event (vf, camel_folder_summary_get_folder (rmi->summary));
		}

		camel_message_info_unref (rmi);
	}

	return res;
}

static gboolean
vee_info_set_user_tag (CamelMessageInfo *mi,
                       const gchar *name,
                       const gchar *value)
{
	gint res = FALSE;

	if (mi->uid) {
		CamelMessageInfo *rmi = camel_folder_summary_get (((CamelVeeMessageInfo *) mi)->orig_summary, mi->uid + 8);
		CamelVeeFolder *vf = (CamelVeeFolder *) camel_folder_summary_get_folder (mi->summary);
		gboolean ignore_changes = !CAMEL_IS_VTRASH_FOLDER (vf);

		HANDLE_NULL_INFO (FALSE);

		/* ignore changes done in the folder itself,
		 * unless it's a vTrash or vJunk folder */
		if (ignore_changes)
			camel_vee_folder_ignore_next_changed_event (vf, camel_folder_summary_get_folder (rmi->summary));

		res = camel_message_info_set_user_tag (rmi, name, value);

		if (ignore_changes) {
			if (res)
				vee_summary_notify_mi_changed (vf, mi);
			else
				camel_vee_folder_remove_from_ignore_changed_event (vf, camel_folder_summary_get_folder (rmi->summary));
		}

		camel_message_info_unref (rmi);
	}

	return res;
}

static gboolean
vee_info_set_flags (CamelMessageInfo *mi,
                    guint32 flags,
                    guint32 set)
{
	gint res = FALSE;
	CamelVeeFolder *vf = CAMEL_VEE_FOLDER (camel_folder_summary_get_folder (mi->summary));

	/* first update original message info... */
	if (mi->uid) {
		CamelMessageInfo *rmi = camel_folder_summary_get (((CamelVeeMessageInfo *) mi)->orig_summary, mi->uid + 8);
		gboolean ignore_changes = !CAMEL_IS_VTRASH_FOLDER (vf);

		HANDLE_NULL_INFO (FALSE);

		/* ignore changes done in the folder itself,
		 * unless it's a vTrash or vJunk folder */
		if (ignore_changes)
			camel_vee_folder_ignore_next_changed_event (vf, camel_folder_summary_get_folder (rmi->summary));

		camel_folder_freeze (camel_folder_summary_get_folder (rmi->summary));
		res = camel_message_info_set_flags (rmi, flags, set);
		camel_folder_thaw (camel_folder_summary_get_folder (rmi->summary));

		if (res) {
			/* update flags on itself too */
			camel_folder_summary_replace_flags (mi->summary, mi);
		}

		if (ignore_changes) {
			if (res)
				vee_summary_notify_mi_changed (vf, mi);
			else
				camel_vee_folder_remove_from_ignore_changed_event (vf, camel_folder_summary_get_folder (rmi->summary));
		}

		camel_message_info_unref (rmi);
	}

	/* Do not call parent class' info_set_flags, to not do flood
	 * of change notifications, rather wait for a notification
	 * from original folder, and propagate the change in counts
	 * through camel_vee_summary_replace_flags().
	*/
	/*if (res)
		CAMEL_FOLDER_SUMMARY_CLASS (camel_vee_summary_parent_class)->info_set_flags (mi, flags, set);*/

	return res;
}

static CamelMessageInfo *
message_info_from_uid (CamelFolderSummary *s,
                       const gchar *uid)
{
	CamelMessageInfo *info;

	info = camel_folder_summary_peek_loaded (s, uid);
	if (!info) {
		CamelVeeMessageInfo *vinfo;
		CamelFolder *orig_folder;

		/* This function isn't really nice. But no great way
		 * But in vfolder case, this may not be so bad, as vuid has the hash in first 8 bytes.
		 * So this just compares the entire string only if it belongs to the same folder.
		 * Otherwise, the first byte itself would return in strcmp, saving the CPU.
		 */
		if (!camel_folder_summary_check_uid (s, uid)) {
			d (
				g_message ("Unable to find %s in the summary of %s", uid,
				camel_folder_get_full_name (camel_folder_summary_get_folder (s->folder))));
			return NULL;
		}

		/* Create the info and load it, its so easy. */
		info = camel_message_info_new (s);
		info->dirty = FALSE;
		info->uid = camel_pstring_strdup (uid);

		orig_folder = camel_vee_folder_get_vee_uid_folder (
			(CamelVeeFolder *) camel_folder_summary_get_folder (s), uid);
		g_return_val_if_fail (orig_folder != NULL, NULL);

		vinfo = (CamelVeeMessageInfo *) info;
		vinfo->orig_summary = orig_folder->summary;

		g_object_ref (vinfo->orig_summary);
		camel_message_info_ref (info);

		camel_folder_summary_insert (s, info, FALSE);
	}

	return info;
}

static void
vee_summary_finalize (GObject *object)
{
	CamelVeeSummaryPrivate *priv;

	priv = CAMEL_VEE_SUMMARY_GET_PRIVATE (object);

	g_hash_table_destroy (priv->vuids_by_subfolder);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_vee_summary_parent_class)->finalize (object);
}

static void
camel_vee_summary_class_init (CamelVeeSummaryClass *class)
{
	GObjectClass *object_class;
	CamelFolderSummaryClass *folder_summary_class;

	g_type_class_add_private (class, sizeof (CamelVeeSummaryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = vee_summary_finalize;

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
	vee_summary->priv = CAMEL_VEE_SUMMARY_GET_PRIVATE (vee_summary);

	vee_summary->priv->vuids_by_subfolder = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) NULL,
		(GDestroyNotify) g_hash_table_destroy);
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
camel_vee_summary_new (CamelFolder *parent)
{
	CamelFolderSummary *summary;
	CamelStore *parent_store;
	const gchar *full_name;

	summary = g_object_new (CAMEL_TYPE_VEE_SUMMARY, "folder", parent, NULL);
	summary->flags |= CAMEL_FOLDER_SUMMARY_IN_MEMORY_ONLY;

	/* not using DB for vee folder summaries, drop the table */
	full_name = camel_folder_get_full_name (parent);
	parent_store = camel_folder_get_parent_store (parent);
	camel_db_delete_folder (parent_store->cdb_w, full_name, NULL);

	return summary;
}

static void
get_uids_for_subfolder (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
	g_hash_table_insert (user_data, (gpointer) camel_pstring_strdup (key), GINT_TO_POINTER (1));
}

/**
 * camel_vee_summary_get_uids_for_subfolder:
 *
 * FIXME Document me!
 *
 * Since: 3.6
 **/
GHashTable *
camel_vee_summary_get_uids_for_subfolder (CamelVeeSummary *summary,
                                          CamelFolder *subfolder)
{
	GHashTable *vuids, *known_uids;

	g_return_val_if_fail (CAMEL_IS_VEE_SUMMARY (summary), NULL);
	g_return_val_if_fail (CAMEL_IS_FOLDER (subfolder), NULL);

	camel_folder_summary_lock (&summary->summary);

	/* uses direct hash, because strings are supposed to be from the string pool */
	known_uids = g_hash_table_new_full (g_direct_hash, g_direct_equal, (GDestroyNotify) camel_pstring_free, NULL);

	vuids = g_hash_table_lookup (summary->priv->vuids_by_subfolder, subfolder);
	if (vuids) {
		g_hash_table_foreach (vuids, get_uids_for_subfolder, known_uids);
	}

	camel_folder_summary_unlock (&summary->summary);

	return known_uids;
}

/* unref returned pointer with camel_message_info_unref() */
CamelVeeMessageInfo *
camel_vee_summary_add (CamelVeeSummary *s,
                       CamelVeeMessageInfoData *mi_data)
{
	CamelVeeMessageInfo *vmi;
	const gchar *vuid;
	CamelVeeSubfolderData *sf_data;
	CamelFolder *orig_folder;
	GHashTable *vuids;

	g_return_val_if_fail (CAMEL_IS_VEE_SUMMARY (s), NULL);
	g_return_val_if_fail (CAMEL_IS_VEE_MESSAGE_INFO_DATA (mi_data), NULL);

	camel_folder_summary_lock (&s->summary);

	sf_data = camel_vee_message_info_data_get_subfolder_data (mi_data);
	vuid = camel_vee_message_info_data_get_vee_message_uid (mi_data);
	orig_folder = camel_vee_subfolder_data_get_folder (sf_data);

	vmi = (CamelVeeMessageInfo *) camel_folder_summary_peek_loaded (&s->summary, vuid);
	if (vmi) {
		/* Possible that the entry is loaded, see if it has the summary */
		d (g_message ("%s - already there\n", vuid));
		if (!vmi->orig_summary)
			vmi->orig_summary = g_object_ref (orig_folder->summary);

		camel_folder_summary_unlock (&s->summary);

		return vmi;
	}

	vmi = (CamelVeeMessageInfo *) camel_message_info_new (&s->summary);
	vmi->orig_summary = g_object_ref (orig_folder->summary);
	vmi->info.uid = (gchar *) camel_pstring_strdup (vuid);

	camel_message_info_ref (vmi);

	vuids = g_hash_table_lookup (s->priv->vuids_by_subfolder, orig_folder);
	if (vuids) {
		g_hash_table_insert (vuids, (gpointer) camel_pstring_strdup (vuid), GINT_TO_POINTER (1));
	} else {
		vuids = g_hash_table_new_full (g_direct_hash, g_direct_equal, (GDestroyNotify) camel_pstring_free, NULL);
		g_hash_table_insert (vuids, (gpointer) camel_pstring_strdup (vuid), GINT_TO_POINTER (1));
		g_hash_table_insert (s->priv->vuids_by_subfolder, orig_folder, vuids);
	}

	camel_folder_summary_insert (&s->summary, (CamelMessageInfo *) vmi, FALSE);
	camel_folder_summary_unlock (&s->summary);

	return vmi;
}

/**
 * camel_vee_summary_remove:
 *
 * FIXME Document me!
 *
 * Since: 3.6
 **/
void
camel_vee_summary_remove (CamelVeeSummary *summary,
                          const gchar *vuid,
                          CamelFolder *subfolder)
{
	CamelMessageInfo *mi;
	GHashTable *vuids;

	g_return_if_fail (CAMEL_IS_VEE_SUMMARY (summary));
	g_return_if_fail (vuid != NULL);
	g_return_if_fail (subfolder != NULL);

	camel_folder_summary_lock (&summary->summary);

	vuids = g_hash_table_lookup (summary->priv->vuids_by_subfolder, subfolder);
	if (vuids) {
		g_hash_table_remove (vuids, vuid);
		if (!g_hash_table_size (vuids))
			g_hash_table_remove (summary->priv->vuids_by_subfolder, subfolder);
	}

	mi = camel_folder_summary_peek_loaded (&summary->summary, vuid);

	camel_folder_summary_remove_uid (&summary->summary, vuid);

	if (mi) {
		/* under twice, the first for camel_folder_summary_peek_loaded(),
		 * the second to actually free the mi */
		camel_message_info_unref (mi);
		camel_message_info_unref (mi);
	}

	camel_folder_summary_unlock (&summary->summary);
}

/**
 * camel_vee_summary_replace_flags:
 * @summary: a #CamelVeeSummary
 * @uid: a message UID to update flags for
 *
 * Makes sure @summary flags on @uid corresponds to those 
 * in the subfolder of vee-folder, and updates internal counts
 * on @summary as well.
 *
 * Since: 3.6
 **/
void
camel_vee_summary_replace_flags (CamelVeeSummary *summary,
                                 const gchar *uid)
{
	CamelMessageInfo *mi;

	g_return_if_fail (CAMEL_IS_VEE_SUMMARY (summary));
	g_return_if_fail (uid != NULL);

	camel_folder_summary_lock (&summary->summary);

	mi = camel_folder_summary_get (&summary->summary, uid);
	if (!mi) {
		camel_folder_summary_unlock (&summary->summary);
		return;
	}

	camel_folder_summary_replace_flags (&summary->summary, mi);
	camel_message_info_unref (mi);

	camel_folder_summary_unlock (&summary->summary);
}

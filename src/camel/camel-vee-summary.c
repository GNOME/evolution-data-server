/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-folder.h"
#include "camel-store.h"
#include "camel-vee-folder.h"
#include "camel-vee-message-info.h"
#include "camel-vee-store.h"
#include "camel-vtrash-folder.h"
#include "camel-string-utils.h"

#include "camel-vee-summary.h"

#define d(x)

struct _CamelVeeSummaryPrivate {
	/* CamelFolder * => GHashTable * of gchar *vuid */
	GHashTable *vuids_by_subfolder;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelVeeSummary, camel_vee_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static CamelMessageInfo *
message_info_from_uid (CamelFolderSummary *s,
                       const gchar *uid)
{
	CamelMessageInfo *info;

	info = camel_folder_summary_peek_loaded (s, uid);
	if (!info) {
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

		orig_folder = camel_vee_folder_dup_vee_uid_folder (CAMEL_VEE_FOLDER (camel_folder_summary_get_folder (s)), uid);
		g_return_val_if_fail (orig_folder != NULL, NULL);

		/* Create the info and load it, its so easy. */
		info = camel_vee_message_info_new (s, camel_folder_get_folder_summary (orig_folder), uid);

		camel_message_info_set_dirty (info, FALSE);

		camel_folder_summary_add (s, info, TRUE);

		g_clear_object (&orig_folder);
	}

	return info;
}

static gboolean
vee_summary_prepare_fetch_all (CamelFolderSummary *summary,
			       GError **error)
{
	GHashTableIter iter;
	gpointer key, value;
	CamelVeeSummary *vee_summary;

	g_return_val_if_fail (CAMEL_IS_VEE_SUMMARY (summary), FALSE);

	camel_folder_summary_lock (summary);

	vee_summary = CAMEL_VEE_SUMMARY (summary);

	g_hash_table_iter_init (&iter, vee_summary->priv->vuids_by_subfolder);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		CamelFolder *subfolder = key;
		GHashTable *vuids = value;

		if (subfolder && vuids && g_hash_table_size (vuids) > 50) {
			CamelFolderSummary *subsummary;

			subsummary = camel_folder_get_folder_summary (subfolder);
			if (subsummary) {
				if (!camel_folder_summary_prepare_fetch_all (subsummary, error)) {
					camel_folder_summary_unlock (summary);
					return FALSE;
				}
			}
		}
	}

	camel_folder_summary_unlock (summary);

	return CAMEL_FOLDER_SUMMARY_CLASS (camel_vee_summary_parent_class)->prepare_fetch_all (summary, error);
}

static void
vee_summary_finalize (GObject *object)
{
	CamelVeeSummaryPrivate *priv;

	priv = CAMEL_VEE_SUMMARY (object)->priv;

	g_hash_table_destroy (priv->vuids_by_subfolder);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_vee_summary_parent_class)->finalize (object);
}

static void
camel_vee_summary_class_init (CamelVeeSummaryClass *class)
{
	GObjectClass *object_class;
	CamelFolderSummaryClass *folder_summary_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = vee_summary_finalize;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_type = CAMEL_TYPE_VEE_MESSAGE_INFO;
	folder_summary_class->message_info_from_uid = message_info_from_uid;
	folder_summary_class->prepare_fetch_all = vee_summary_prepare_fetch_all;
}

static void
camel_vee_summary_init (CamelVeeSummary *vee_summary)
{
	vee_summary->priv = camel_vee_summary_get_instance_private (vee_summary);

	vee_summary->priv->vuids_by_subfolder = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) g_object_unref,
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

	summary = g_object_new (CAMEL_TYPE_VEE_SUMMARY, "folder", parent, NULL);
	camel_folder_summary_set_flags (summary, camel_folder_summary_get_flags (summary) | CAMEL_FOLDER_SUMMARY_IN_MEMORY_ONLY);

	return summary;
}

static void
get_uids_for_subfolder (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
	g_hash_table_add (user_data, (gpointer) camel_pstring_strdup (key));
}

/**
 * camel_vee_summary_get_uids_for_subfolder: (skip)
 * @summary: a #CamelVeeSummary
 * @subfolder: a #CamelFolder
 *
 * Returns a hash table of all virtual message info UID-s known to the @summary.
 * The key of the hash table is the virtual message info UID, the value is
 * only the number 1.
 *
 * Returns: (element-type utf8 gint) (transfer container): a #GHashTable with
 *    all the virtual mesasge info UID-s knwn to the @summary.
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

	camel_folder_summary_lock (CAMEL_FOLDER_SUMMARY (summary));

	/* uses direct hash, because strings are supposed to be from the string pool */
	known_uids = g_hash_table_new_full (g_direct_hash, g_direct_equal, (GDestroyNotify) camel_pstring_free, NULL);

	vuids = g_hash_table_lookup (summary->priv->vuids_by_subfolder, subfolder);
	if (vuids) {
		g_hash_table_foreach (vuids, get_uids_for_subfolder, known_uids);
	}

	camel_folder_summary_unlock (CAMEL_FOLDER_SUMMARY (summary));

	return known_uids;
}

/**
 * camel_vee_summary_add: (skip)
 * @summary: the CamelVeeSummary
 * @subfolder: a #CamelFolder the @vuid references
 * @vuid: a message UID as referenced in the @summary
 *
 * Unref returned pointer with g_object_unref()
 *
 * Returns: (transfer full): A new #CamelVeeMessageInfo object.
 *
 * Since: 3.58
 **/
CamelVeeMessageInfo *
camel_vee_summary_add (CamelVeeSummary *summary,
		       CamelFolder *subfolder,
		       const gchar *vuid)
{
	CamelVeeMessageInfo *vmi;
	GHashTable *vuids;

	g_return_val_if_fail (CAMEL_IS_VEE_SUMMARY (summary), NULL);
	g_return_val_if_fail (CAMEL_IS_FOLDER (subfolder), NULL);
	g_return_val_if_fail (vuid != NULL, NULL);

	camel_folder_summary_lock (CAMEL_FOLDER_SUMMARY (summary));

	vmi = (CamelVeeMessageInfo *) camel_folder_summary_peek_loaded (CAMEL_FOLDER_SUMMARY (summary), vuid);
	if (vmi) {
		/* Possible that the entry is loaded, see if it has the summary */
		d (g_message ("%s - already there\n", vuid));
		g_warn_if_fail (camel_vee_message_info_get_original_summary (vmi) != NULL);

		camel_folder_summary_unlock (CAMEL_FOLDER_SUMMARY (summary));

		return vmi;
	}

	vmi = (CamelVeeMessageInfo *) camel_vee_message_info_new (CAMEL_FOLDER_SUMMARY (summary), camel_folder_get_folder_summary (subfolder), vuid);

	vuids = g_hash_table_lookup (summary->priv->vuids_by_subfolder, subfolder);
	if (vuids) {
		g_hash_table_insert (vuids, (gpointer) camel_pstring_strdup (vuid), GINT_TO_POINTER (1));
	} else {
		vuids = g_hash_table_new_full (g_direct_hash, g_direct_equal, (GDestroyNotify) camel_pstring_free, NULL);
		g_hash_table_insert (vuids, (gpointer) camel_pstring_strdup (vuid), GINT_TO_POINTER (1));
		g_hash_table_insert (summary->priv->vuids_by_subfolder, g_object_ref (subfolder), vuids);
	}

	camel_folder_summary_add (CAMEL_FOLDER_SUMMARY (summary), (CamelMessageInfo *) vmi, TRUE);
	camel_folder_summary_unlock (CAMEL_FOLDER_SUMMARY (summary));

	return vmi;
}

/**
 * camel_vee_summary_remove: (skip)
 * @summary: a #CamelVeeSummary
 * @subfolder: a #CamelFolder to which @vuid belongs
 * @vuid: a virtual message info UID to remove
 *
 * Removes the given @vuid of the @subfolder from the @summary.
 *
 * Since: 3.58
 **/
void
camel_vee_summary_remove (CamelVeeSummary *summary,
			  CamelFolder *subfolder,
			  const gchar *vuid)
{
	GHashTable *vuids;

	g_return_if_fail (CAMEL_IS_VEE_SUMMARY (summary));
	g_return_if_fail (vuid != NULL);
	g_return_if_fail (subfolder != NULL);

	camel_folder_summary_lock (CAMEL_FOLDER_SUMMARY (summary));

	vuids = g_hash_table_lookup (summary->priv->vuids_by_subfolder, subfolder);
	if (vuids) {
		g_hash_table_remove (vuids, vuid);
		if (!g_hash_table_size (vuids))
			g_hash_table_remove (summary->priv->vuids_by_subfolder, subfolder);
	}

	camel_folder_summary_remove_uid (CAMEL_FOLDER_SUMMARY (summary), vuid);

	camel_folder_summary_unlock (CAMEL_FOLDER_SUMMARY (summary));
}

/**
 * camel_vee_summary_replace_flags: (skip)
 * @vsummary: a #CamelVeeSummary
 * @vuid: a message UID to update flags for
 *
 * Makes sure @vsummary flags on @uid corresponds to those
 * in the subfolder of vee-folder, and updates internal counts
 * on @vsummary as well.
 *
 * Returns: whether any count changed
 *
 * Since: 3.58
 **/
gboolean
camel_vee_summary_replace_flags (CamelVeeSummary *vsummary,
                                 const gchar *vuid)
{
	CamelFolderSummary *summary;
	CamelMessageInfo *mi;
	gboolean changed;

	g_return_val_if_fail (CAMEL_IS_VEE_SUMMARY (vsummary), FALSE);
	g_return_val_if_fail (vuid != NULL, FALSE);

	summary = CAMEL_FOLDER_SUMMARY (vsummary);

	camel_folder_summary_lock (summary);

	mi = camel_folder_summary_get (summary, vuid);
	if (!mi) {
		camel_folder_summary_unlock (summary);
		return FALSE;
	}

	changed = camel_folder_summary_replace_flags (summary, camel_message_info_get_uid (mi), camel_message_info_get_flags (mi));
	g_clear_object (&mi);

	camel_folder_summary_unlock (summary);

	return changed;
}

/**
 * camel_vee_summary_to_match_index: (skip)
 * @self: a #CamelVeeSummary
 *
 * Converts current content of the @self into an array
 * of match indexes #CamelStoreSearchIndex.
 *
 * Returns: (transfer full): a new #CamelStoreSearchIndex representing
 *    real folders belonging to the @self
 *
 * Since: 3.58
 **/
CamelStoreSearchIndex *
camel_vee_summary_to_match_index (CamelVeeSummary *self)
{
	CamelFolderSummary *summary;
	CamelStoreSearchIndex *match_index;
	GHashTableIter sfiter;
	gpointer key = NULL, value = NULL;

	g_return_val_if_fail (CAMEL_IS_VEE_SUMMARY (self), NULL);

	summary = CAMEL_FOLDER_SUMMARY (self);
	match_index = camel_store_search_index_new ();

	camel_folder_summary_lock (summary);

	g_hash_table_iter_init (&sfiter, self->priv->vuids_by_subfolder);
	while (g_hash_table_iter_next (&sfiter, &key, &value)) {
		CamelFolder *folder = key;
		GHashTable *vuids = value;
		CamelStore *store = camel_folder_get_parent_store (folder);
		GHashTableIter viter;
		guint32 folder_id;

		if (!store)
			continue;

		folder_id = camel_store_db_get_folder_id (camel_store_get_db (store), camel_folder_get_full_name (folder));
		if (!folder_id)
			continue;

		g_hash_table_iter_init (&viter, vuids);
		while (g_hash_table_iter_next (&viter, &key, NULL)) {
			const gchar *vuid = key;

			/* the first 8 letters is a hash of the folder it belongs to, thus skip it to get the real UID */
			camel_store_search_index_add (match_index, store, folder_id, vuid + 8);
		}
	}

	camel_folder_summary_unlock (summary);

	return match_index;
}

/**
 * camel_vee_summary_dup_subfolders: (skip)
 * @self: a #CamelVeeSummary
 *
 * Returns a new #GHashTable of the #CamelFolder-s as the key of all the subfolders
 * for which the @self has any message info.
 *
 * Returns: (transfer full) (element-type CamelFolder CamelFolder): all subfolders
 *    the @self has any message info from
 *
 * Since: 3.58
 **/
GHashTable *
camel_vee_summary_dup_subfolders (CamelVeeSummary *self)
{
	CamelFolderSummary *summary;
	GHashTable *subfolders;
	GHashTableIter sfiter;
	gpointer key = NULL;

	g_return_val_if_fail (CAMEL_IS_VEE_SUMMARY (self), NULL);

	summary = CAMEL_FOLDER_SUMMARY (self);
	subfolders = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, NULL);

	camel_folder_summary_lock (summary);

	g_hash_table_iter_init (&sfiter, self->priv->vuids_by_subfolder);
	while (g_hash_table_iter_next (&sfiter, &key, NULL)) {
		CamelFolder *folder = key;

		g_hash_table_add (subfolders, g_object_ref (folder));
	}

	camel_folder_summary_unlock (summary);

	return subfolders;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
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

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-exception.h"
#include "camel-folder-search.h"
#include "camel-mime-message.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-vee-folder.h"
#include "camel-vee-store.h"	/* for open flags */
#include "camel-vee-summary.h"
#include "camel-string-utils.h"
#include "camel-vee-folder.h"
#include "camel-vtrash-folder.h"

#define d(x)
#define dd(x) (camel_debug ("vfolder")?(x):0)

#define CAMEL_VEE_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_VEE_FOLDER, CamelVeeFolderPrivate))

struct _CamelVeeFolderPrivate {
	gboolean destroyed;
	GList *folders;			/* lock using subfolder_lock before changing/accessing */
	GList *folders_changed;		/* for list of folders that have changed between updates */

	GMutex *summary_lock;		/* for locking vfolder summary */
	GMutex *subfolder_lock;		/* for locking the subfolder list */
	GMutex *changed_lock;		/* for locking the folders-changed list */
	gint unread_vfolder;
};

struct _update_data {
	CamelFolder *source;
	CamelVeeFolder *vee_folder;
	gchar hash[8];
	CamelVeeFolder *folder_unmatched;
	GHashTable *unmatched_uids;
	gboolean rebuilt, correlating;
};

struct _folder_changed_msg {
	CamelSessionThreadMsg msg;
	CamelFolderChangeInfo *changes;
	CamelFolder *sub;
	CamelVeeFolder *vee_folder;
};

G_DEFINE_TYPE (CamelVeeFolder, camel_vee_folder, CAMEL_TYPE_FOLDER)

/* must be called with summary_lock held */
static CamelVeeMessageInfo *
vee_folder_add_uid (CamelVeeFolder *vf,
                    CamelFolder *f,
                    const gchar *inuid,
                    const gchar hash[8])
{
	CamelVeeMessageInfo *mi = NULL;

	mi = camel_vee_summary_add ((CamelVeeSummary *)((CamelFolder *)vf)->summary, f->summary, (gchar *)inuid, hash);
	return mi;
}

/* same as vee_folder_add_uid, only returns whether uid was added or not */
static gboolean
vee_folder_add_uid_test (CamelVeeFolder *vf,
                         CamelFolder *f,
                         const gchar *inuid,
                         const gchar hash[8])
{
	CamelVeeMessageInfo *mi;

	mi = vee_folder_add_uid (vf, f, inuid, hash);

	if (mi != NULL)
		camel_message_info_free ((CamelMessageInfo *) mi);

	return mi != NULL;
}

/* A "correlating" expression has the property that whether a message matches
 * depends on the other messages being searched.  folder_changed_change on a
 * vfolder with a correlating expression may not make all the necessary updates,
 * so the query is redone on the entire changed source folder the next time
 * the vfolder is opened.
 *
 * The only current example of a correlating expression is one that uses
 * "match-threads". */
static gboolean
expression_is_correlating (const gchar *expr)
{
	/* XXX: Actually parse the expression to avoid triggering on
	 * "match-threads" in the text the user is searching for! */
	return (strstr (expr, "match-threads") != NULL);
}

/* Hold all these with summary lock and unmatched summary lock held */
static void
folder_changed_add_uid (CamelFolder *sub, const gchar *uid, const gchar hash[8], CamelVeeFolder *vf, gboolean use_db)
{
	CamelFolder *folder = (CamelFolder *)vf;
	CamelVeeMessageInfo *vinfo;
	const gchar *vuid;
	gchar *oldkey;
	gpointer oldval;
	gint n;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;

	vinfo = vee_folder_add_uid (vf, sub, uid, hash);
	if (vinfo == NULL)
		return;

	vuid = camel_pstring_strdup (camel_message_info_uid (vinfo));
	camel_message_info_free ((CamelMessageInfo *) vinfo);
	if (use_db) {
		CamelException ex = CAMEL_EXCEPTION_INITIALISER;
		camel_db_add_to_vfolder_transaction (folder->parent_store->cdb_w, folder->full_name, vuid, &ex);
		camel_exception_clear (&ex);
	}
	camel_folder_change_info_add_uid (vf->changes,  vuid);
	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !CAMEL_IS_VEE_FOLDER (sub) && folder_unmatched != NULL) {
		if (g_hash_table_lookup_extended (unmatched_uids, vuid, (gpointer *)&oldkey, &oldval)) {
			n = GPOINTER_TO_INT (oldval);
			g_hash_table_insert (unmatched_uids, oldkey, GINT_TO_POINTER (n+1));
		} else {
			g_hash_table_insert (unmatched_uids, g_strdup (vuid), GINT_TO_POINTER (1));
		}
		vinfo = (CamelVeeMessageInfo *)camel_folder_get_message_info ((CamelFolder *)folder_unmatched, vuid);
		if (vinfo) {
			CamelException ex = CAMEL_EXCEPTION_INITIALISER;
			camel_folder_change_info_remove_uid (folder_unmatched->changes, vuid);
			camel_db_delete_uid_from_vfolder_transaction (folder->parent_store->cdb_w, ((CamelFolder *)folder_unmatched)->full_name, vuid, &ex);
			camel_folder_summary_remove_uid_fast (((CamelFolder *)folder_unmatched)->summary, vuid);
			camel_folder_free_message_info ((CamelFolder *)folder_unmatched, (CamelMessageInfo *)vinfo);
			camel_exception_clear (&ex);
		}
	}

	camel_pstring_free (vuid);
}

static void
folder_changed_remove_uid (CamelFolder *sub, const gchar *uid, const gchar hash[8], gint keep, CamelVeeFolder *vf, gboolean use_db)
{
	CamelFolder *folder = (CamelFolder *)vf;
	gchar *vuid, *oldkey;
	gpointer oldval;
	gint n;
	CamelVeeMessageInfo *vinfo;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;

	vuid = alloca (strlen (uid)+9);
	memcpy (vuid, hash, 8);
	strcpy (vuid+8, uid);

	camel_folder_change_info_remove_uid (vf->changes, vuid);
	if (use_db) {
		/* FIXME[disk-summary] Handle exception */
		CamelException ex = CAMEL_EXCEPTION_INITIALISER;
		camel_db_delete_uid_from_vfolder_transaction (
			folder->parent_store->cdb_w,
			folder->full_name, vuid, &ex);
		camel_exception_clear (&ex);
	}
	camel_folder_summary_remove_uid_fast (folder->summary, vuid);

	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !CAMEL_IS_VEE_FOLDER (sub) && folder_unmatched != NULL) {
		if (keep) {
			if (g_hash_table_lookup_extended (unmatched_uids, vuid, (gpointer *)&oldkey, &oldval)) {
				n = GPOINTER_TO_INT (oldval);
				if (n == 1) {
					g_hash_table_remove (unmatched_uids, oldkey);
					if (vee_folder_add_uid_test (folder_unmatched, sub, uid, hash))
						camel_folder_change_info_add_uid (folder_unmatched->changes, oldkey);
					g_free (oldkey);
				} else {
					g_hash_table_insert (unmatched_uids, oldkey, GINT_TO_POINTER (n-1));
				}
			} else {
				if (vee_folder_add_uid_test (folder_unmatched, sub, uid, hash))
					camel_folder_change_info_add_uid (folder_unmatched->changes, vuid);
			}
		} else {
			if (g_hash_table_lookup_extended (unmatched_uids, vuid, (gpointer *)&oldkey, &oldval)) {
				g_hash_table_remove (unmatched_uids, oldkey);
				g_free (oldkey);
			}

			vinfo = (CamelVeeMessageInfo *)camel_folder_get_message_info ((CamelFolder *)folder_unmatched, vuid);
			if (vinfo) {
				CamelException ex = CAMEL_EXCEPTION_INITIALISER;
				camel_folder_change_info_remove_uid (folder_unmatched->changes, vuid);
				camel_db_delete_uid_from_vfolder_transaction (folder->parent_store->cdb_w, ((CamelFolder *)folder_unmatched)->full_name, vuid, &ex);
				camel_folder_summary_remove_uid_fast (((CamelFolder *)folder_unmatched)->summary, vuid);
				camel_folder_free_message_info ((CamelFolder *)folder_unmatched, (CamelMessageInfo *)vinfo);
				camel_exception_clear (&ex);
			}
		}
	}
}

static void
folder_changed_change_uid (CamelFolder *sub, const gchar *uid, const gchar hash[8], CamelVeeFolder *vf, gboolean use_db)
{
	gchar *vuid;
	CamelVeeMessageInfo *vinfo, *uinfo = NULL;
	CamelMessageInfo *info;
	CamelFolder *folder = (CamelFolder *)vf;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	vuid = alloca (strlen (uid)+9);
	memcpy (vuid, hash, 8);
	strcpy (vuid+8, uid);

	vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid (folder->summary, vuid);
	if (folder_unmatched != NULL)
		uinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid (((CamelFolder *)folder_unmatched)->summary, vuid);
	if (vinfo || uinfo) {
		info = camel_folder_get_message_info (sub, uid);
		if (info) {
			if (vinfo) {
				camel_folder_change_info_change_uid (vf->changes, vuid);
				camel_message_info_free ((CamelMessageInfo *)vinfo);
			}

			if (uinfo) {
				camel_folder_change_info_change_uid (folder_unmatched->changes, vuid);
				camel_message_info_free ((CamelMessageInfo *)uinfo);
			}

			camel_folder_free_message_info (sub, info);
		} else {
			if (vinfo) {
				folder_changed_remove_uid (sub, uid, hash, FALSE, vf, use_db);
				camel_message_info_free ((CamelMessageInfo *)vinfo);
			}
			if (uinfo)
				camel_message_info_free ((CamelMessageInfo *)uinfo);
		}
	}
}

static void
folder_changed_change (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_changed_msg *m = (struct _folder_changed_msg *)msg;
	CamelFolder *sub = m->sub;
	CamelFolder *folder = (CamelFolder *)m->vee_folder;
	CamelVeeFolder *vf = m->vee_folder;
	CamelFolderChangeInfo *changes = m->changes;
	gchar *vuid = NULL, hash[8];
	const gchar *uid;
	CamelVeeMessageInfo *vinfo;
	gint i, vuidlen = 0;
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;
	GPtrArray *matches_added = NULL, /* newly added, that match */
		*matches_changed = NULL, /* newly changed, that now match */
		*newchanged = NULL,
		*changed;
	GPtrArray *always_changed = NULL;
	GHashTable *matches_hash;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;
	GPtrArray *present = NULL;

	/* See vee_folder_rebuild_folder. */
	gboolean correlating = expression_is_correlating (vf->expression);

	/* Check the folder hasn't beem removed while we weren't watching */
	camel_vee_folder_lock (vf, CVF_SUBFOLDER_LOCK);
	if (g_list_find (CAMEL_VEE_FOLDER_GET_PRIVATE (vf)->folders, sub) == NULL) {
		camel_vee_folder_unlock (vf, CVF_SUBFOLDER_LOCK);
		return;
	}

	camel_vee_folder_hash_folder (sub, hash);

	/* Lookup anything before we lock anything, to avoid deadlock with build_folder */

	/* Find newly added that match */
	if (changes->uid_added->len > 0) {
		CamelException ex = CAMEL_EXCEPTION_INITIALISER;
		dd (printf (" Searching for added matches '%s'\n", vf->expression));
		matches_added = camel_folder_search_by_uids (sub, vf->expression, changes->uid_added, &ex);
		camel_exception_clear (&ex);
	}

	/* TODO:
	   In this code around here, we can work out if the search will affect the changes
	   we had, and only re-search against them if they might have */

	/* Search for changed items that newly match, but only if we dont have them */
	changed = changes->uid_changed;
	if (changed->len > 0) {
		CamelException ex = CAMEL_EXCEPTION_INITIALISER;
		dd (printf (" Searching for changed matches '%s'\n", vf->expression));

		if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0) {
			newchanged = g_ptr_array_new ();
			always_changed = g_ptr_array_new ();
			for (i=0;i<changed->len;i++) {
				uid = changed->pdata[i];
				if (strlen (uid)+9 > vuidlen) {
					vuidlen = strlen (uid)+64;
					vuid = g_realloc (vuid, vuidlen);
				}
				memcpy (vuid, hash, 8);
				strcpy (vuid+8, uid);
				vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid (folder->summary, vuid);
				if (vinfo == NULL) {
					g_ptr_array_add (newchanged, (gchar *)uid);
				} else {
					g_ptr_array_add (always_changed, (gchar *)uid);
					camel_message_info_free ((CamelMessageInfo *)vinfo);
				}
			}
			changed = newchanged;
		}

		if (changed->len)
			matches_changed = camel_folder_search_by_uids (sub, vf->expression, changed, &ex);
		camel_exception_clear (&ex);
		if (always_changed && always_changed->len)
			present = camel_folder_search_by_uids (sub, vf->expression, always_changed, &ex);
		camel_exception_clear (&ex);
	}

	camel_vee_folder_lock (vf, CVF_SUMMARY_LOCK);

	if (folder_unmatched != NULL)
		camel_vee_folder_lock (folder_unmatched, CVF_SUMMARY_LOCK);

	if (matches_changed || matches_added || changes->uid_removed->len||present)
		camel_db_begin_transaction (folder->parent_store->cdb_w, NULL);

	dd (printf ("Vfolder '%s' subfolder changed '%s'\n", folder->full_name, sub->full_name));
	dd (printf (" changed %u added %u removed %u\n", changes->uid_changed->len, changes->uid_added->len, changes->uid_removed->len));

	/* Always remove removed uid's, in any case */
	for (i=0;i<changes->uid_removed->len;i++) {
		dd (printf ("  removing uid '%s'\n", (gchar *)changes->uid_removed->pdata[i]));
		folder_changed_remove_uid (sub, changes->uid_removed->pdata[i], hash, FALSE, vf, !correlating);
	}

	/* Add any newly matched or to unmatched folder if they dont */
	if (matches_added) {
		matches_hash = g_hash_table_new (g_str_hash, g_str_equal);
		for (i=0;i<matches_added->len;i++) {
			dd (printf (" %s", (gchar *)matches_added->pdata[i]));
			g_hash_table_insert (matches_hash, matches_added->pdata[i], matches_added->pdata[i]);
		}
		for (i=0;i<changes->uid_added->len;i++) {
			uid = changes->uid_added->pdata[i];
			if (g_hash_table_lookup (matches_hash, uid)) {
				dd (printf ("  adding uid '%s' [newly matched]\n", (gchar *)uid));
				folder_changed_add_uid (sub, uid, hash, vf, !correlating);
			} else if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
				if (strlen (uid)+9 > vuidlen) {
					vuidlen = strlen (uid)+64;
					vuid = g_realloc (vuid, vuidlen);
				}
				memcpy (vuid, hash, 8);
				strcpy (vuid+8, uid);

				if (!CAMEL_IS_VEE_FOLDER (sub) && folder_unmatched != NULL && g_hash_table_lookup (unmatched_uids, vuid) == NULL) {
					dd (printf ("  adding uid '%s' to Unmatched [newly unmatched]\n", (gchar *)uid));
					vinfo = (CamelVeeMessageInfo *)camel_folder_get_message_info ((CamelFolder *)folder_unmatched, vuid);
					if (vinfo == NULL) {
						if (vee_folder_add_uid_test (folder_unmatched, sub, uid, hash))
							camel_folder_change_info_add_uid (folder_unmatched->changes, vuid);
					} else {
						camel_folder_free_message_info ((CamelFolder *)folder_unmatched, (CamelMessageInfo *)vinfo);
					}
				}
			}
		}
		g_hash_table_destroy (matches_hash);
	}

	/* Change any newly changed */
	if (always_changed) {
		if (correlating) {
			/* Messages may be pulled in by the correlation even if
			 * they do not match the expression individually, so it
			 * would be wrong to preemptively remove anything here.
			 * vee_folder_rebuild_folder will make any necessary removals
			 * when it re-queries the entire source folder. */
			for (i=0;i<always_changed->len;i++)
				folder_changed_change_uid (sub, always_changed->pdata[i], hash, vf, !correlating);
		} else {
			GHashTable *ht_present = g_hash_table_new (g_str_hash, g_str_equal);

			for (i=0;present && i<present->len;i++) {
				folder_changed_change_uid (sub, present->pdata[i], hash, vf, !correlating);
				g_hash_table_insert (ht_present, present->pdata[i], present->pdata[i]);
			}

			for (i=0; i<always_changed->len; i++) {
				if (!present || !g_hash_table_lookup (ht_present, always_changed->pdata[i]))
					/* XXX: IIUC, these messages haven't been deleted from the
					 * source folder, so shouldn't "keep" be set to TRUE? */
					folder_changed_remove_uid (sub, always_changed->pdata[i], hash, TRUE, vf, !correlating);
			}

			g_hash_table_destroy (ht_present);
		}
		g_ptr_array_free (always_changed, TRUE);
	}

	/* Change/add/remove any changed */
	if (changes->uid_changed->len) {
		/* If we are auto-updating, then re-check changed uids still match */
		dd (printf (" Vfolder %supdate\nuids match:", (vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO)?"auto-":""));
		matches_hash = g_hash_table_new (g_str_hash, g_str_equal);
		for (i=0;matches_changed && i<matches_changed->len;i++) {
			dd (printf (" %s", (gchar *)matches_changed->pdata[i]));
			g_hash_table_insert (matches_hash, matches_changed->pdata[i], matches_changed->pdata[i]);
		}
		dd (printf ("\n"));

		for (i=0;i<changed->len;i++) {
			uid = changed->pdata[i];
			if (strlen (uid)+9 > vuidlen) {
				vuidlen = strlen (uid)+64;
				vuid = g_realloc (vuid, vuidlen);
			}
			memcpy (vuid, hash, 8);
			strcpy (vuid+8, uid);
			vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid (folder->summary, vuid);
			if (vinfo == NULL) {
				if (g_hash_table_lookup (matches_hash, uid)) {
					/* A uid we dont have, but now it matches, add it */
					dd (printf ("  adding uid '%s' [newly matched]\n", uid));
					folder_changed_add_uid (sub, uid, hash, vf, !correlating);
				} else {
					/* A uid we still don't have, just change it (for unmatched) */
					folder_changed_change_uid (sub, uid, hash, vf, !correlating);
				}
			} else {
				if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0
				    || g_hash_table_lookup (matches_hash, uid)) {
					/* still match, or we're not auto-updating, change event, (if it changed) */
					dd (printf ("  changing uid '%s' [still matches]\n", uid));
					folder_changed_change_uid (sub, uid, hash, vf, !correlating);
				} else {
					/* No longer matches, remove it, but keep it in unmatched (potentially) */
					dd (printf ("  removing uid '%s' [did match]\n", uid));
					folder_changed_remove_uid (sub, uid, hash, TRUE, vf, !correlating);
				}
				camel_message_info_free ((CamelMessageInfo *)vinfo);
			}
		}
		g_hash_table_destroy (matches_hash);
	} else {
		/* stuff didn't match but it changed - check unmatched folder for changes */
		for (i=0;i<changed->len;i++)
			folder_changed_change_uid (sub, changed->pdata[i], hash, vf, !correlating);
	}

	if (folder_unmatched != NULL) {
		if (camel_folder_change_info_changed (folder_unmatched->changes)) {
			unmatched_changes = folder_unmatched->changes;
			folder_unmatched->changes = camel_folder_change_info_new ();
		}

		camel_vee_folder_unlock (folder_unmatched, CVF_SUMMARY_LOCK);
	}

	if (camel_folder_change_info_changed (vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new ();
	}

	if (matches_changed || matches_added || changes->uid_removed->len || present)
		camel_db_end_transaction (folder->parent_store->cdb_w, NULL);
	camel_vee_folder_unlock (vf, CVF_SUMMARY_LOCK);

	/* Cleanup stuff on our folder */
	if (matches_added)
		camel_folder_search_free (sub, matches_added);
	if (present)
		camel_folder_search_free (sub, present);

	if (matches_changed)
		camel_folder_search_free (sub, matches_changed);

	camel_vee_folder_unlock (vf, CVF_SUBFOLDER_LOCK);

	/* cleanup the rest */
	if (newchanged)
		g_ptr_array_free (newchanged, TRUE);

	g_free (vuid);

	if (unmatched_changes) {
		camel_object_trigger_event ((CamelObject *)folder_unmatched, "folder_changed", unmatched_changes);
		camel_folder_change_info_free (unmatched_changes);
	}

	/* Add to folders_changed if we need to call vee_folder_rebuild_folder, which
	 * could be the case for two reasons:
	 * - We changed the vfolder and it is not auto-updating.  Need to re-sync.
	 * - Vfolder is correlating.  Changes to non-matching source messages
	 *   won't be processed here and won't show up in vf_changes but may
	 *   still affect the vfolder contents (e.g., non-matching messages
	 *   added to a matching thread), so we re-run the query on the whole
	 *   source folder.  (For match-threads, it may be enough to do this if
	 *   changes->uid_added->len > 0, but I'm not completely sure and I'd
	 *   rather be safe than sorry.)
	 */
	if ((vf_changes && (vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0) || correlating) {
		camel_vee_folder_lock (vf, CVF_CHANGED_LOCK);
		if (g_list_find (vf->priv->folders_changed, sub) == NULL)
			vf->priv->folders_changed = g_list_prepend (vf->priv->folders_changed, sub);
		camel_vee_folder_unlock (vf, CVF_CHANGED_LOCK);
	}

	if (vf_changes) {
		camel_object_trigger_event ((CamelObject *)vf, "folder_changed", vf_changes);
		camel_folder_change_info_free (vf_changes);
	}
}

static void
subfolder_renamed_update (CamelVeeFolder *vf, CamelFolder *sub, gchar hash[8])
{
	gint count, i;
	CamelFolderChangeInfo *changes = NULL;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;
	CamelFolderSummary *ssummary = sub->summary;

	camel_vee_folder_lock (vf, CVF_SUMMARY_LOCK);

	camel_folder_summary_prepare_fetch_all (((CamelFolder *)vf)->summary, NULL);

	count = camel_folder_summary_count (((CamelFolder *)vf)->summary);
	for (i=0;i<count;i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index (((CamelFolder *)vf)->summary, i);
		CamelVeeMessageInfo *vinfo;

		if (mi == NULL)
			continue;

		if (mi->summary == ssummary) {
			gchar *uid = (gchar *)camel_message_info_uid (mi);
			gchar *oldkey;
			gpointer oldval;

			camel_folder_change_info_remove_uid (vf->changes, uid);
			camel_folder_summary_remove (((CamelFolder *)vf)->summary, (CamelMessageInfo *)mi);

			/* works since we always append on the end */
			i--;
			count--;

			vinfo = vee_folder_add_uid (vf, sub, uid+8, hash);
			if (vinfo) {
				camel_folder_change_info_add_uid (vf->changes, camel_message_info_uid (vinfo));

				/* check unmatched uid's table for any matches */
				if (vf == folder_unmatched
				    && g_hash_table_lookup_extended (unmatched_uids, uid, (gpointer *)&oldkey, &oldval)) {
					g_hash_table_remove (unmatched_uids, oldkey);
					g_hash_table_insert (unmatched_uids, g_strdup (camel_message_info_uid (vinfo)), oldval);
					g_free (oldkey);
				}

				camel_message_info_free ((CamelMessageInfo *) vinfo);
			}
		}

		camel_message_info_free ((CamelMessageInfo *)mi);
	}

	if (camel_folder_change_info_changed (vf->changes)) {
		changes = vf->changes;
		vf->changes = camel_folder_change_info_new ();
	}

	camel_vee_folder_unlock (vf, CVF_SUMMARY_LOCK);

	if (changes) {
		camel_object_trigger_event ((CamelObject *)vf, "folder_changed", changes);
		camel_folder_change_info_free (changes);
	}
}

static void
folder_changed_free (CamelSession *session, CamelSessionThreadMsg *msg)
{
	struct _folder_changed_msg *m = (struct _folder_changed_msg *)msg;

	camel_folder_change_info_free (m->changes);
	g_object_unref (m->vee_folder);
	g_object_unref (m->sub);
}

static CamelSessionThreadOps folder_changed_ops = {
	folder_changed_change,
	folder_changed_free,
};

static gint
vee_folder_rebuild_folder (CamelVeeFolder *vee_folder,
                           CamelFolder *source,
                           CamelException *ex);

static void
unmatched_check_uid (gchar *uidin, gpointer value, struct _update_data *u)
{
	gchar *uid;
	gint n;

	uid = alloca (strlen (uidin)+9);
	memcpy (uid, u->hash, 8);
	strcpy (uid+8, uidin);
	n = GPOINTER_TO_INT (g_hash_table_lookup (u->unmatched_uids, uid));
	if (n == 0) {
		if (vee_folder_add_uid_test (u->folder_unmatched, u->source, uidin, u->hash))
			camel_folder_change_info_add_uid (u->folder_unmatched->changes, uid);
	} else {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_uid (((CamelFolder *)u->folder_unmatched)->summary, uid);
		if (mi) {
			CamelException ex = CAMEL_EXCEPTION_INITIALISER;
			camel_db_delete_uid_from_vfolder_transaction (((CamelFolder *)u->folder_unmatched)->parent_store->cdb_w, ((CamelFolder *)u->folder_unmatched)->full_name, uid, &ex);
			camel_folder_summary_remove_uid_fast (((CamelFolder *)u->folder_unmatched)->summary, uid);
			camel_folder_change_info_remove_uid (u->folder_unmatched->changes, uid);
			camel_message_info_free ((CamelMessageInfo *)mi);
			camel_exception_clear (&ex);
		}
	}
}

static void
folder_added_uid (gchar *uidin, gpointer value, struct _update_data *u)
{
	CamelVeeMessageInfo *mi;
	gchar *oldkey;
	gpointer oldval;
	gint n;

	if ((mi = vee_folder_add_uid (u->vee_folder, u->source, uidin, u->hash)) != NULL) {
		camel_folder_change_info_add_uid (u->vee_folder->changes, camel_message_info_uid (mi));
		/* FIXME[disk-summary] Handle exceptions */
		/* FIXME[disk-summary] Make all these as transactions, just
		 * testing atm */
		if (u->rebuilt && !u->correlating) {
			CamelException ex = CAMEL_EXCEPTION_INITIALISER;
			camel_db_add_to_vfolder_transaction (((CamelFolder *) u->vee_folder)->parent_store->cdb_w, ((CamelFolder *) u->vee_folder)->full_name, camel_message_info_uid (mi), &ex);
			camel_exception_clear (&ex);
		}
		if (!CAMEL_IS_VEE_FOLDER (u->source) && u->unmatched_uids != NULL) {
			if (g_hash_table_lookup_extended (u->unmatched_uids, camel_message_info_uid (mi), (gpointer *)&oldkey, &oldval)) {
				n = GPOINTER_TO_INT (oldval);
				g_hash_table_insert (u->unmatched_uids, oldkey, GINT_TO_POINTER (n+1));
			} else {
				g_hash_table_insert (u->unmatched_uids, g_strdup (camel_message_info_uid (mi)), GINT_TO_POINTER (1));
			}
		}

		camel_message_info_free ((CamelMessageInfo *) mi);
	}
}

static gint
count_result (CamelFolderSummary *summary,
              const gchar *query,
              CamelException *ex)
{
	CamelFolder *folder = summary->folder;
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	guint32 count=0;
	gchar *expr = g_strdup_printf ("(and %s %s)", vf->expression ? vf->expression : "", query);
	GList *node;
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vf);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		count += camel_folder_count_by_expression (f, expr, ex);
		node = node->next;
	}

	g_free (expr);
	return count;
}

static	CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s,
                      CamelException *ex)
{
	CamelFIRecord * record = g_new0 (CamelFIRecord, 1);
	CamelDB *db;
	gchar *table_name;
	guint32 visible, unread, deleted, junked, junked_not_deleted;

	/* We do this during write, so lets use write handle, though we gonna read */
	db = s->folder->parent_store->cdb_w;
	table_name = s->folder->full_name;

	record->folder_name = table_name;

	/* we always write out the current version */
	record->version = 13;  /* FIXME: CAMEL_FOLDER_SUMMARY_VERSION; */
	record->flags  = s->flags;
	record->nextuid = s->nextuid;
	record->time = s->time;

	record->saved_count = s->uids->len;
	camel_object_get (s->folder, NULL,
				 CAMEL_FOLDER_DELETED, &deleted,
				 CAMEL_FOLDER_VISIBLE, &visible,
				 CAMEL_FOLDER_JUNKED, &junked,
				 CAMEL_FOLDER_JUNKED_NOT_DELETED, &junked_not_deleted,
				 CAMEL_FOLDER_UNREAD, &unread, NULL);
	if (1) { /* We always would do this. Just refactor the code again. */
		/*!(((CamelVeeSummary *) s)->force_counts) && !g_getenv ("FORCE_VFOLDER_COUNT")) {*/
		/* We should be in sync always. so use the count. Don't search.*/
		record->junk_count = s->junk_count;
		record->deleted_count = s->deleted_count;
		record->unread_count = s->unread_count;

		if (((CamelVeeSummary *)s)->fake_visible_count)
			record->visible_count = ((CamelVeeSummary *)s)->fake_visible_count;
		else
			record->visible_count = s->visible_count;
		((CamelVeeSummary *)s)->fake_visible_count = 0;

		record->jnd_count = s->junk_not_deleted_count;
	} else {
		/* Either first time, or by force we search the count */
		s->junk_count = count_result (s, "(match-all (system-flag  \"junk\"))", ex);
		s->deleted_count = count_result (s, "(match-all (system-flag  \"deleted\"))", ex);
		s->unread_count = count_result (s, "(match-all (not (system-flag  \"Seen\")))", ex);
		s->visible_count = count_result (s, "(match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\"))))", ex);
		s->junk_not_deleted_count = count_result (s, "(match-all (and (not (system-flag \"deleted\")) (system-flag \"junk\")))", ex);

		record->junk_count = s->junk_count;
		record->deleted_count = s->deleted_count;
		record->unread_count = s->unread_count;
		record->visible_count = s->visible_count;
		record->jnd_count = s->junk_not_deleted_count;
	}

	d (printf ("%s %d %d %d %d %d\n", s->folder->full_name, record->junk_count, record->deleted_count, record->unread_count, record->visible_count, record->jnd_count));
	return record;
}

static void
folder_changed (CamelFolder *sub,
                CamelFolderChangeInfo *changes,
                CamelVeeFolder *vee_folder)
{
	CamelVeeFolderClass *class;

	class = CAMEL_VEE_FOLDER_GET_CLASS (vee_folder);
	class->folder_changed (vee_folder, sub, changes);
}

/* track vanishing folders */
static void
subfolder_deleted (CamelFolder *folder,
                   gpointer event_data,
                   CamelVeeFolder *vee_folder)
{
	camel_vee_folder_remove_folder (vee_folder, folder);
}

static void
folder_renamed (CamelFolder *sub,
                const gchar *old,
                CamelVeeFolder *vee_folder)
{
	CamelVeeFolderClass *class;

	class = CAMEL_VEE_FOLDER_GET_CLASS (vee_folder);
	class->folder_renamed (vee_folder, sub, old);
}

static void
vee_folder_stop_folder (CamelVeeFolder *vf, CamelFolder *sub)
{
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vf);
	gint i;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	camel_vee_folder_lock (vf, CVF_SUBFOLDER_LOCK);

	camel_vee_folder_lock (vf, CVF_CHANGED_LOCK);
	p->folders_changed = g_list_remove (p->folders_changed, sub);
	camel_vee_folder_unlock (vf, CVF_CHANGED_LOCK);

	if (g_list_find (p->folders, sub) == NULL) {
		camel_vee_folder_unlock (vf, CVF_SUBFOLDER_LOCK);
		return;
	}

	camel_object_unhook_event ((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc) folder_changed, vf);
	camel_object_unhook_event ((CamelObject *)sub, "deleted", (CamelObjectEventHookFunc) subfolder_deleted, vf);
	camel_object_unhook_event ((CamelObject *)sub, "renamed", (CamelObjectEventHookFunc) folder_renamed, vf);

	p->folders = g_list_remove (p->folders, sub);

	/* undo the freeze state that we have imposed on this source folder */
	camel_folder_lock (CAMEL_FOLDER (vf), CF_CHANGE_LOCK);
	for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *)vf); i++)
		camel_folder_thaw (sub);
	camel_folder_unlock (CAMEL_FOLDER (vf), CF_CHANGE_LOCK);

	camel_vee_folder_unlock (vf, CVF_SUBFOLDER_LOCK);

	if (folder_unmatched != NULL) {
		CamelVeeFolderPrivate *up = CAMEL_VEE_FOLDER_GET_PRIVATE (folder_unmatched);

		camel_vee_folder_lock (folder_unmatched, CVF_SUBFOLDER_LOCK);
		/* if folder deleted, then blow it away from unmatched always, and remove all refs to it */
		if (sub->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED) {
			while (g_list_find (up->folders, sub)) {
				up->folders = g_list_remove (up->folders, sub);
				g_object_unref (sub);

				/* undo the freeze state that Unmatched has imposed on this source folder */
				camel_folder_lock (CAMEL_FOLDER (folder_unmatched), CF_CHANGE_LOCK);
				for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *)folder_unmatched); i++)
					camel_folder_thaw (sub);
				camel_folder_unlock (CAMEL_FOLDER (folder_unmatched), CF_CHANGE_LOCK);
			}
		} else if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
			if (g_list_find (up->folders, sub) != NULL) {
				up->folders = g_list_remove (up->folders, sub);
				g_object_unref (sub);

				/* undo the freeze state that Unmatched has imposed on this source folder */
				camel_folder_lock (CAMEL_FOLDER (folder_unmatched), CF_CHANGE_LOCK);
				for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *)folder_unmatched); i++)
					camel_folder_thaw (sub);
				camel_folder_unlock (CAMEL_FOLDER (folder_unmatched), CF_CHANGE_LOCK);
			}
		}
		camel_vee_folder_unlock (folder_unmatched, CVF_SUBFOLDER_LOCK);
	}

	if (CAMEL_IS_VEE_FOLDER (sub))
		return;

	g_object_unref (sub);
}

static void
vee_folder_finalize (GObject *object)
{
	CamelVeeFolder *vf;
	CamelVeeFolder *folder_unmatched;
	GList *node;
	CamelFIRecord * record;

	vf = CAMEL_VEE_FOLDER (object);
	vf->priv->destroyed = TRUE;

	folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	/* Save the counts to DB */
	if (!vf->deleted) {
		CamelException ex = CAMEL_EXCEPTION_INITIALISER;
		record = summary_header_to_db (((CamelFolder *)vf)->summary, NULL);
		camel_db_write_folder_info_record (((CamelFolder *) vf)->parent_store->cdb_w, record, &ex);
		g_free (record);
		camel_exception_clear (&ex);
	}

	/* This may invoke sub-classes with partially destroyed state, they must deal with this */
	if (vf == folder_unmatched) {
		for (node = vf->priv->folders;node;node = g_list_next (node))
			g_object_unref (node->data);
	} else {
		/* FIXME[disk-summary] See if it is really reqd */
		camel_folder_freeze ((CamelFolder *)vf);
		while (vf->priv->folders) {
			CamelFolder *f = vf->priv->folders->data;
			vee_folder_stop_folder (vf, f);
		}
		camel_folder_thaw ((CamelFolder *)vf);
	}

	g_free (vf->expression);

	g_list_free (vf->priv->folders);
	g_list_free (vf->priv->folders_changed);

	camel_folder_change_info_free (vf->changes);
	g_object_unref (vf->search);

	g_mutex_free (vf->priv->summary_lock);
	g_mutex_free (vf->priv->subfolder_lock);
	g_mutex_free (vf->priv->changed_lock);
	g_hash_table_destroy (vf->hashes);

	/* Chain up to parent's finalize () method. */
	G_OBJECT_CLASS (camel_vee_folder_parent_class)->finalize (object);
}

/* This entire code will be useless, since we sync the counts always. */
static gint
vee_folder_getv (CamelObject *object,
                 CamelException *ex,
                 CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	gint i;
	guint32 tag;
	gint unread = -1, deleted = 0, junked = 0, visible = 0, count = -1, junked_not_deleted = -1;

	for (i=0;i<args->argc;i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		/* NB: this is a copy of camel-folder.c with the unread count logic altered.
		   makes sure its still atomically calculated */
		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_FOLDER_ARG_UNREAD:
		case CAMEL_FOLDER_ARG_DELETED:
		case CAMEL_FOLDER_ARG_JUNKED:
		case CAMEL_FOLDER_ARG_JUNKED_NOT_DELETED:
		case CAMEL_FOLDER_ARG_VISIBLE:

			if (vf->expression && vf->priv->unread_vfolder == -1)
				camel_vee_summary_load_check_unread_vfolder ((CamelVeeSummary *)folder->summary);

			/* This is so we can get the values atomically, and also so we can calculate them only once */
			if (unread == -1) {
				gint j;
				CamelMessageInfoBase *info;
				CamelVeeMessageInfo *vinfo;

				unread = deleted = visible = junked = junked_not_deleted = 0;
				camel_folder_summary_prepare_fetch_all (folder->summary, ex);
				count = camel_folder_summary_count (folder->summary);
				for (j=0; j<count; j++) {
					if ((info = (CamelMessageInfoBase *) camel_folder_summary_index (folder->summary, j))) {
						guint32 flags;

						vinfo = (CamelVeeMessageInfo *) info;
						flags = vinfo->old_flags; /* ? vinfo->old_flags : camel_message_info_flags (info); */

						if ((flags & (CAMEL_MESSAGE_SEEN)) == 0)
							unread++;
						if (flags & CAMEL_MESSAGE_DELETED)
							deleted++;
						if (flags & CAMEL_MESSAGE_JUNK) {
							junked++;
								if (!(flags & CAMEL_MESSAGE_DELETED))
									junked_not_deleted++;
						}
						if ((flags & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) == 0)
							visible++;
						camel_message_info_free (info);
					}
				}
			}

			switch (tag & CAMEL_ARG_TAG) {
			case CAMEL_FOLDER_ARG_UNREAD:
				if (vf->priv->unread_vfolder == 1)
					count = unread == -1 ? 0 : unread - junked_not_deleted;
				else
					count = unread == -1 ? 0 : unread;
				break;
			case CAMEL_FOLDER_ARG_DELETED:
				count = deleted == -1 ? 0 : deleted;
				break;
			case CAMEL_FOLDER_ARG_JUNKED:
				count = junked == -1 ? 0 : junked;
				break;
			case CAMEL_FOLDER_ARG_JUNKED_NOT_DELETED:
				count = junked_not_deleted == -1 ? 0 : junked_not_deleted;
				break;
			case CAMEL_FOLDER_ARG_VISIBLE:
				if (vf->priv->unread_vfolder == 1)
					count = unread == -1 ? 0 : unread - junked_not_deleted;
				else
					count = visible == -1 ? 0 : visible;

				break;
			}
			folder->summary->unread_count = unread == -1 ? 0 : unread;
			folder->summary->deleted_count = deleted == -1 ? 0 : deleted;
			junked = folder->summary->junk_count = junked == -1 ? 0 : junked;
			folder->summary->junk_not_deleted_count = junked_not_deleted == -1 ? 0 : junked_not_deleted;
			folder->summary->visible_count = visible == -1 ? 0 : visible;
			*arg->ca_int = count;
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	return ((CamelObjectClass *)camel_vee_folder_parent_class)->getv (object, ex, args);
}

static gboolean
vee_folder_refresh_info (CamelFolder *folder,
                         CamelException *ex)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vf);
	GList *node, *list;
	gboolean success = TRUE;

	camel_vee_folder_lock (vf, CVF_CHANGED_LOCK);
	list = p->folders_changed;
	p->folders_changed = NULL;
	camel_vee_folder_unlock (vf, CVF_CHANGED_LOCK);

	node = list;
	while (node) {
		CamelFolder *f = node->data;

		if (camel_vee_folder_rebuild_folder (vf, f, ex) == -1) {
			success = FALSE;
			break;
		}

		node = node->next;
	}

	g_list_free (list);

	return success;
}

static gboolean
vee_folder_sync (CamelFolder *folder,
                 gboolean expunge,
                 CamelException *ex)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vf);
	GList *node;

	if (((CamelVeeSummary *)folder->summary)->fake_visible_count)
		folder->summary->visible_count = ((CamelVeeSummary *)folder->summary)->fake_visible_count;
	((CamelVeeSummary *)folder->summary)->fake_visible_count = 0;

	camel_vee_folder_lock (vf, CVF_SUBFOLDER_LOCK);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		if (!camel_folder_sync (f, expunge, ex)) {
			if (strncmp (camel_exception_get_description (ex), "no such table", 13)) {
				const gchar *desc;

				camel_object_get (f, NULL, CAMEL_OBJECT_DESCRIPTION, &desc, NULL);
				camel_exception_setv (ex, ex->id, _("Error storing '%s': %s"), desc, ex->desc);
				g_warning ("%s", camel_exception_get_description (ex));
			} else
				camel_exception_clear (ex);
		}

		/* auto update vfolders shouldn't need a rebuild */
/*		if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0 */
/*		    && camel_vee_folder_rebuild_folder (vf, f, ex) == -1) */
/*			break; */

		node = node->next;
	}

#if 0
	/* Seems like we are doing something wrong with this, as folder_changed happens after this, the counts are misleading.
	 * Anyways we do a force sync on exit, it should be all fine.
	  */
	record = summary_header_to_db (folder->summary, ex);
	camel_db_write_folder_info_record (folder->parent_store->cdb, record, ex);
	g_free (record);
#endif
	/* It makes no sense to clear the folders_changed list without
	 * actually rebuilding. */
#if 0
	if (node == NULL) {
		camel_vee_folder_lock (vf, CVF_CHANGED_LOCK);
		g_list_free (p->folders_changed);
		p->folders_changed = NULL;
		camel_vee_folder_unlock (vf, CVF_CHANGED_LOCK);
	}
#endif
	if (vf->priv->unread_vfolder == 1) {
		/* Cleanup Junk/Trash uids */
		GSList *del = NULL;
		gint i, count;

		camel_folder_summary_prepare_fetch_all (folder->summary, ex);
		count = camel_folder_summary_count (folder->summary);
		for (i=0; i < count; i++) {
			CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index (folder->summary, i);
			if (mi->old_flags & CAMEL_MESSAGE_DELETED) {
				del = g_slist_prepend (del, (gpointer) camel_pstring_strdup (((CamelMessageInfo *)mi)->uid));
				camel_folder_summary_remove_index_fast (folder->summary, i);
				count--;
				i--;

			}
			camel_message_info_free (mi);
		}
		camel_db_delete_vuids (folder->parent_store->cdb_w, folder->full_name, "", del, ex);
		g_slist_foreach (del, (GFunc) camel_pstring_free, NULL);
		g_slist_free (del);
	}
	camel_vee_folder_unlock (vf, CVF_SUBFOLDER_LOCK);

	camel_object_state_write (vf);

	return TRUE;
}

static gboolean
vee_folder_expunge (CamelFolder *folder,
                    CamelException *ex)
{
	/* Force it to rebuild the counts, when some folders were expunged. */
	((CamelVeeSummary *) folder->summary)->force_counts = TRUE;

	return CAMEL_FOLDER_GET_CLASS (folder)->sync (folder, TRUE, ex);
}

static CamelMimeMessage *
vee_folder_get_message (CamelFolder *folder,
                        const gchar *uid,
                        CamelException *ex)
{
	CamelVeeMessageInfo *mi;
	CamelMimeMessage *msg = NULL;

	mi = (CamelVeeMessageInfo *)camel_folder_summary_uid (folder->summary, uid);
	if (mi) {
		msg = camel_folder_get_message (mi->summary->folder, camel_message_info_uid (mi)+8, ex);
		camel_message_info_free ((CamelMessageInfo *)mi);
	} else {
		camel_exception_setv (
			ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
			_("No such message %s in %s"), uid,
			folder->name);
	}

	return msg;
}

static gboolean
vee_folder_append_message (CamelFolder *folder,
                           CamelMimeMessage *message,
                           const CamelMessageInfo *info,
                           gchar **appended_uid,
                            CamelException *ex)
{
	camel_exception_set (
		ex, CAMEL_EXCEPTION_SYSTEM,
		_("Cannot copy or move messages into a Virtual Folder"));

	return FALSE;
}

static gboolean
vee_folder_transfer_messages_to (CamelFolder *folder,
                                 GPtrArray *uids,
                                 CamelFolder *dest,
                                 GPtrArray **transferred_uids,
                                 gboolean delete_originals,
                                 CamelException *ex)
{
	camel_exception_set (
		ex, CAMEL_EXCEPTION_SYSTEM,
		_("Cannot copy or move messages into a Virtual Folder"));

	return FALSE;
}

static GPtrArray *
vee_folder_search_by_expression (CamelFolder *folder,
                                 const gchar *expression,
                                 CamelException *ex)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new ();
	gchar *expr;
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vf);
	GHashTable *searched = g_hash_table_new (NULL, NULL);
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	if (vf != folder_unmatched)
		expr = g_strdup_printf ("(and %s %s)", vf->expression ? vf->expression : "", expression);
	else
		expr = g_strdup (expression);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		gint i;
		gchar hash[8];

		/* make sure we only search each folder once - for unmatched folder to work right */
		if (g_hash_table_lookup (searched, f) == NULL) {
			camel_vee_folder_hash_folder (f, hash);
			matches = camel_folder_search_by_expression (f, expr, ex);
			if (camel_exception_is_set (ex) && strncmp (camel_exception_get_description (ex), "no such table", 13)) {
				camel_exception_clear (ex);
			}
			if (matches) {
				for (i = 0; i < matches->len; i++) {
					gchar *uid = matches->pdata[i], *vuid;

					vuid = g_malloc (strlen (uid)+9);
					memcpy (vuid, hash, 8);
					strcpy (vuid+8, uid);
					g_ptr_array_add (result, (gpointer) camel_pstring_strdup (vuid));
					g_free (vuid);
				}
				camel_folder_search_free (f, matches);
			}
			g_hash_table_insert (searched, f, f);
		}
		node = g_list_next (node);
	}

	g_free (expr);

	g_hash_table_destroy (searched);
	d (printf ("returning %d\n", result->len));
	return result;
}

static GPtrArray *
vee_folder_search_by_uids (CamelFolder *folder,
                           const gchar *expression,
                           GPtrArray *uids,
                           CamelException *ex)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new ();
	GPtrArray *folder_uids = g_ptr_array_new ();
	gchar *expr;
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vf);
	GHashTable *searched = g_hash_table_new (NULL, NULL);

	camel_vee_folder_lock (vf, CVF_SUBFOLDER_LOCK);

	expr = g_strdup_printf ("(and %s %s)", vf->expression ? vf->expression : "", expression);
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		gint i;
		gchar hash[8];

		/* make sure we only search each folder once - for unmatched folder to work right */
		if (g_hash_table_lookup (searched, f) == NULL) {
			camel_vee_folder_hash_folder (f, hash);

			/* map the vfolder uid's to the source folder uid's first */
			g_ptr_array_set_size (folder_uids, 0);
			for (i=0;i<uids->len;i++) {
				gchar *uid = uids->pdata[i];

				if (strlen (uid) >= 8 && strncmp (uid, hash, 8) == 0)
					g_ptr_array_add (folder_uids, uid+8);
			}
			if (folder_uids->len > 0) {
				matches = camel_folder_search_by_uids (f, expr, folder_uids, ex);
				if (matches) {
					for (i = 0; i < matches->len; i++) {
						gchar *uid = matches->pdata[i], *vuid;

						vuid = g_malloc (strlen (uid)+9);
						memcpy (vuid, hash, 8);
						strcpy (vuid+8, uid);
						g_ptr_array_add (result, (gpointer) camel_pstring_strdup (vuid));
						g_free (vuid);
					}
					camel_folder_search_free (f, matches);
				} else {
					g_warning ("Search failed: %s", camel_exception_get_description (ex));
				}
			}
			g_hash_table_insert (searched, f, f);
		}
		node = g_list_next (node);
	}

	g_free (expr);
	camel_vee_folder_unlock (vf, CVF_SUBFOLDER_LOCK);

	g_hash_table_destroy (searched);
	g_ptr_array_free (folder_uids, TRUE);

	return result;
}

static guint32
vee_folder_count_by_expression (CamelFolder *folder,
                                const gchar *expression,
                                CamelException *ex)
{
	GList *node;
	gchar *expr;
	guint32 count = 0;
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vf);
	GHashTable *searched = g_hash_table_new (NULL, NULL);
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	if (vf != folder_unmatched)
		expr = g_strdup_printf ("(and %s %s)", vf->expression ? vf->expression : "", expression);
	else
		expr = g_strdup (expression);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		/* make sure we only search each folder once - for unmatched folder to work right */
		if (g_hash_table_lookup (searched, f) == NULL) {
			count += camel_folder_count_by_expression (f, expr, ex);
			g_hash_table_insert (searched, f, f);
		}
		node = g_list_next (node);
	}

	g_free (expr);

	g_hash_table_destroy (searched);
	return count;
}

static void
vee_folder_delete (CamelFolder *folder)
{
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (folder);

	/* NB: this is never called on UNMTACHED */

	camel_vee_folder_lock (CAMEL_VEE_FOLDER (folder), CVF_SUBFOLDER_LOCK);
	while (p->folders) {
		CamelFolder *f = p->folders->data;

		g_object_ref (f);
		camel_vee_folder_unlock (CAMEL_VEE_FOLDER (folder), CVF_SUBFOLDER_LOCK);

		camel_vee_folder_remove_folder ((CamelVeeFolder *)folder, f);
		g_object_unref (f);
		camel_vee_folder_lock (CAMEL_VEE_FOLDER (folder), CVF_SUBFOLDER_LOCK);
	}
	camel_vee_folder_unlock (CAMEL_VEE_FOLDER (folder), CVF_SUBFOLDER_LOCK);

	((CamelFolderClass *)camel_vee_folder_parent_class)->delete (folder);
	((CamelVeeFolder *)folder)->deleted = TRUE;
}

static void
vee_folder_freeze (CamelFolder *folder)
{
	CamelVeeFolder *vfolder = (CamelVeeFolder *)folder;
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vfolder);
	GList *node;

	camel_vee_folder_lock (vfolder, CVF_SUBFOLDER_LOCK);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		camel_folder_freeze (f);
		node = node->next;
	}

	camel_vee_folder_unlock (vfolder, CVF_SUBFOLDER_LOCK);

	/* call parent implementation */
	CAMEL_FOLDER_CLASS (camel_vee_folder_parent_class)->freeze (folder);
}

static void
vee_folder_thaw (CamelFolder *folder)
{
	CamelVeeFolder *vfolder = (CamelVeeFolder *)folder;
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vfolder);
	GList *node;

	camel_vee_folder_lock (vfolder, CVF_SUBFOLDER_LOCK);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		camel_folder_thaw (f);
		node = node->next;
	}

	camel_vee_folder_unlock (vfolder, CVF_SUBFOLDER_LOCK);

	/* call parent implementation */
	CAMEL_FOLDER_CLASS (camel_vee_folder_parent_class)->thaw (folder);
}

static void
vee_folder_set_expression (CamelVeeFolder *vee_folder,
                           const gchar *query)
{
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vee_folder);
	GList *node;
	CamelException ex = CAMEL_EXCEPTION_INITIALISER;

	camel_vee_folder_lock (vee_folder, CVF_SUBFOLDER_LOCK);

	/* no change, do nothing */
	if ((vee_folder->expression && query && strcmp (vee_folder->expression, query) == 0)
	    || (vee_folder->expression == NULL && query == NULL)) {
		camel_vee_folder_unlock (vee_folder, CVF_SUBFOLDER_LOCK);
		return;
	}

	/* Recreate the table when the query changes, only if we are not setting it first */
	if (vee_folder->expression) {
		CamelFolderSummary *s = ((CamelFolder *)vee_folder)->summary;
		camel_folder_summary_clear (s);
		camel_db_recreate_vfolder (((CamelFolder *) vee_folder)->parent_store->cdb_w, ((CamelFolder *) vee_folder)->full_name, &ex);
		camel_exception_clear (&ex);
		s->junk_count = 0;
		s->deleted_count = 0;
		s->unread_count = 0;
		s->visible_count = 0;
		s->junk_not_deleted_count = 0;
	}

	g_free (vee_folder->expression);
	if (query)
		vee_folder->expression = g_strdup (query);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		if (camel_vee_folder_rebuild_folder (vee_folder, f, &ex) == -1)
			break;

		camel_exception_clear (&ex);

		node = node->next;
	}

	camel_exception_clear (&ex);

	camel_vee_folder_lock (vee_folder, CVF_CHANGED_LOCK);
	g_list_free (p->folders_changed);
	p->folders_changed = NULL;
	camel_vee_folder_unlock (vee_folder, CVF_CHANGED_LOCK);

	camel_vee_folder_unlock (vee_folder, CVF_SUBFOLDER_LOCK);
}

static void
vee_folder_add_folder (CamelVeeFolder *vee_folder,
                       CamelFolder *sub)
{
	CamelException ex = CAMEL_EXCEPTION_INITIALISER;

	vee_folder_rebuild_folder (vee_folder, sub, &ex);

	camel_exception_clear (&ex);
}

static void
vee_folder_remove_folder_helper (CamelVeeFolder *vf, CamelFolder *source)
{
	gint i, count, n, still = FALSE, start, last;
	gchar *oldkey;
	CamelFolder *folder = (CamelFolder *)vf;
	gchar hash[8];
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;
	gpointer oldval;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;
	CamelFolderSummary *ssummary = source->summary;
	gint killun = FALSE;

	if (vf == folder_unmatched)
		return;

	if ((source->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED))
		killun = TRUE;

	camel_vee_folder_lock (vf, CVF_SUMMARY_LOCK);

	if (folder_unmatched != NULL) {
		/* check if this folder is still to be part of unmatched */
		if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !killun) {
			camel_vee_folder_lock (folder_unmatched, CVF_SUBFOLDER_LOCK);
			still = g_list_find (CAMEL_VEE_FOLDER_GET_PRIVATE (folder_unmatched)->folders, source) != NULL;
			camel_vee_folder_unlock (folder_unmatched, CVF_SUBFOLDER_LOCK);
			camel_vee_folder_hash_folder (source, hash);
		}

		camel_vee_folder_lock (folder_unmatched, CVF_SUMMARY_LOCK);

		/* See if we just blow all uid's from this folder away from unmatched, regardless */
		if (killun) {
			start = -1;
			last = -1;
			camel_folder_summary_prepare_fetch_all (((CamelFolder *)folder_unmatched)->summary, NULL);
			count = camel_folder_summary_count (((CamelFolder *)folder_unmatched)->summary);
			for (i=0;i<count;i++) {
				CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index (((CamelFolder *)folder_unmatched)->summary, i);

				if (mi) {
					if (mi->summary == ssummary) {
						camel_folder_change_info_remove_uid (folder_unmatched->changes, camel_message_info_uid (mi));
						if (last == -1) {
							last = start = i;
						} else if (last+1 == i) {
							last = i;
						} else {
							camel_folder_summary_remove_range (((CamelFolder *)folder_unmatched)->summary, start, last);
							i -= (last-start)+1;
							start = last = i;
						}
					}
					camel_message_info_free ((CamelMessageInfo *)mi);
				}
			}
			if (last != -1)
				camel_folder_summary_remove_range (((CamelFolder *)folder_unmatched)->summary, start, last);
		}
	}

	/*FIXME: This can be optimized a lot like, searching for UID in the summary uids */
	start = -1;
	last = -1;
	camel_folder_summary_prepare_fetch_all (folder->summary, NULL);
	count = camel_folder_summary_count (folder->summary);
	for (i=0;i<count;i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index (folder->summary, i);
		if (mi) {
			if (mi->summary == ssummary) {
				const gchar *uid = camel_message_info_uid (mi);

				camel_folder_change_info_remove_uid (vf->changes, uid);

				if (last == -1) {
					last = start = i;
				} else if (last+1 == i) {
					last = i;
				} else {
					camel_folder_summary_remove_range (folder->summary, start, last);
					i -= (last-start)+1;
					start = last = i;
				}
				if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && folder_unmatched != NULL) {
					if (still) {
						if (g_hash_table_lookup_extended (unmatched_uids, uid, (gpointer *)&oldkey, &oldval)) {
							n = GPOINTER_TO_INT (oldval);
							if (n == 1) {
								g_hash_table_remove (unmatched_uids, oldkey);
								if (vee_folder_add_uid_test (folder_unmatched, source, oldkey+8, hash)) {
									camel_folder_change_info_add_uid (folder_unmatched->changes, oldkey);
								}
								g_free (oldkey);
							} else {
								g_hash_table_insert (unmatched_uids, oldkey, GINT_TO_POINTER (n-1));
							}
						}
					} else {
						if (g_hash_table_lookup_extended (unmatched_uids, camel_message_info_uid (mi), (gpointer *)&oldkey, &oldval)) {
							g_hash_table_remove (unmatched_uids, oldkey);
							g_free (oldkey);
						}
					}
				}
			}
			camel_message_info_free ((CamelMessageInfo *)mi);
		}
	}

	if (last != -1)
		camel_folder_summary_remove_range (folder->summary, start, last);

	if (folder_unmatched) {
		if (camel_folder_change_info_changed (folder_unmatched->changes)) {
			unmatched_changes = folder_unmatched->changes;
			folder_unmatched->changes = camel_folder_change_info_new ();
		}

		camel_vee_folder_unlock (folder_unmatched, CVF_SUMMARY_LOCK);
	}

	if (camel_folder_change_info_changed (vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new ();
	}

	camel_vee_folder_unlock (vf, CVF_SUMMARY_LOCK);

	if (unmatched_changes) {
		camel_object_trigger_event ((CamelObject *)folder_unmatched, "folder_changed", unmatched_changes);
		camel_folder_change_info_free (unmatched_changes);
	}

	if (vf_changes) {
		camel_object_trigger_event ((CamelObject *)vf, "folder_changed", vf_changes);
		camel_folder_change_info_free (vf_changes);
	}
}

static void
vee_folder_remove_folder (CamelVeeFolder *vee_folder,
                          CamelFolder *sub)
{
	gchar *shash, hash[8];
	CamelVeeFolder *folder_unmatched = vee_folder->parent_vee_store ? vee_folder->parent_vee_store->folder_unmatched : NULL;

	camel_vee_folder_hash_folder (sub, hash);
	vee_folder_remove_folder_helper (vee_folder, sub);
	shash = g_strdup_printf (
		"%c%c%c%c%c%c%c%c",
		hash[0], hash[1], hash[2], hash[3],
		hash[4], hash[5], hash[6], hash[7]);
	if (g_hash_table_lookup (vee_folder->hashes, shash)) {
		g_hash_table_remove (vee_folder->hashes, shash);
	}

	if (folder_unmatched && g_hash_table_lookup (folder_unmatched->hashes, shash)) {
		g_hash_table_remove (folder_unmatched->hashes, shash);
	}

	g_free (shash);
}

static gint
vee_folder_rebuild_folder (CamelVeeFolder *vee_folder,
                           CamelFolder *source,
                           CamelException *ex)
{
	GPtrArray *match, *all;
	GHashTable *allhash, *matchhash, *fullhash;
	GSList *del_list = NULL;
	CamelFolder *folder = (CamelFolder *)vee_folder;
	gint i, n, count, start, last;
	struct _update_data u;
	CamelFolderChangeInfo *vee_folder_changes = NULL, *unmatched_changes = NULL;
	CamelVeeFolder *folder_unmatched = vee_folder->parent_vee_store ? vee_folder->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vee_folder->parent_vee_store ? vee_folder->parent_vee_store->unmatched_uids : NULL;
	CamelFolderSummary *ssummary = source->summary;
	gboolean rebuilded = FALSE;
	gchar *shash;

	/* Since the source of a correlating vfolder has to be requeried in
	 * full every time it changes, caching the results in the db is not
	 * worth the effort.  Thus, DB use is conditioned on !correlating. */
	gboolean correlating = expression_is_correlating (vee_folder->expression);

	if (vee_folder == folder_unmatched)
		return 0;

	camel_vee_folder_hash_folder (source, u.hash);
	shash = g_strdup_printf ("%c%c%c%c%c%c%c%c", u.hash[0], u.hash[1], u.hash[2], u.hash[3], u.hash[4], u.hash[5], u.hash[6], u.hash[7]);
	if (!g_hash_table_lookup (vee_folder->hashes, shash)) {
		g_hash_table_insert (vee_folder->hashes, g_strdup (shash), source->summary);
	}
	if (folder_unmatched && !g_hash_table_lookup (folder_unmatched->hashes, shash)) {
		g_hash_table_insert (folder_unmatched->hashes, g_strdup (shash), source->summary);
	}

	/* if we have no expression, or its been cleared, then act as if no matches */
	if (vee_folder->expression == NULL) {
		match = g_ptr_array_new ();
	} else {
		if (!correlating) {
			/* Load the folder results from the DB. */
			match = camel_vee_summary_get_ids ((CamelVeeSummary *)folder->summary, u.hash);
		}
		if (correlating ||
			/* We take this to mean the results have not been cached.
			 * XXX: It will also trigger if the result set is empty. */
			match == NULL) {
			match = camel_folder_search_by_expression (source, vee_folder->expression, ex);
			if (match == NULL) /* Search failed */
				return 0;
			rebuilded = TRUE;
		}

	}
	dd (printf ("vee_folder_rebuild_folder (%s <- %s %s): match %d, correlating %d, rebuilded %d\n",
		folder->full_name, source->full_name, shash, match->len, correlating, rebuilded));

	u.source = source;
	u.vee_folder = vee_folder;
	u.folder_unmatched = folder_unmatched;
	u.unmatched_uids = unmatched_uids;
	u.rebuilt = rebuilded;
	u.correlating = correlating;

	camel_vee_folder_lock (vee_folder, CVF_SUMMARY_LOCK);

	/* we build 2 hash tables, one for all uid's not matched, the
	   other for all matched uid's, we just ref the real memory */
	matchhash = g_hash_table_new (g_str_hash, g_str_equal);
	for (i=0;i<match->len;i++)
		g_hash_table_insert (matchhash, match->pdata[i], GINT_TO_POINTER (1));

	allhash = g_hash_table_new (g_str_hash, g_str_equal);
	fullhash = g_hash_table_new (g_str_hash, g_str_equal);
	all = camel_folder_summary_array (source->summary);
	for (i=0;i<all->len;i++) {
		if (g_hash_table_lookup (matchhash, all->pdata[i]) == NULL)
			g_hash_table_insert (allhash, all->pdata[i], GINT_TO_POINTER (1));
		g_hash_table_insert (fullhash, all->pdata[i], GINT_TO_POINTER (1));

	}
	/* remove uids that can't be found in the source folder */
	count = match->len;
	for (i=0; i<count; i++) {
		if (!g_hash_table_lookup (fullhash, match->pdata[i])) {
			g_hash_table_remove (matchhash, match->pdata[i]);
			del_list = g_slist_prepend (del_list, match->pdata[i]); /* Free the original */
			g_ptr_array_remove_index_fast (match, i);
			i--;
			count--;
			continue;
		}
	}

	if (folder_unmatched != NULL)
		camel_vee_folder_lock (folder_unmatched, CVF_SUMMARY_LOCK);

	/* scan, looking for "old" uid's to be removed. "old" uid's
	   are those that are from previous added sources (not in
	   current source) */
	start = -1;
	last = -1;
	camel_folder_summary_prepare_fetch_all (folder->summary, ex);
	count = camel_folder_summary_count (folder->summary);
	for (i=0;i<count;i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index (folder->summary, i);

		if (mi) {
			if (mi->summary == ssummary) {
				gchar *uid = (gchar *)camel_message_info_uid (mi), *oldkey;
				gpointer oldval;

				if (g_hash_table_lookup (matchhash, uid+8) == NULL) {
					if (last == -1) {
						last = start = i;
					} else if (last+1 == i) {
						last = i;
					} else {
						camel_folder_summary_remove_range (folder->summary, start, last);
						i -= (last-start)+1;
						start = last = i;
					}
					camel_folder_change_info_remove_uid (vee_folder->changes, camel_message_info_uid (mi));
					if (!CAMEL_IS_VEE_FOLDER (source)
					    && unmatched_uids != NULL
					    && g_hash_table_lookup_extended (unmatched_uids, uid, (gpointer *)&oldkey, &oldval)) {
						n = GPOINTER_TO_INT (oldval);
						if (n == 1) {
							g_hash_table_remove (unmatched_uids, oldkey);
							g_free (oldkey);
						} else {
							g_hash_table_insert (unmatched_uids, oldkey, GINT_TO_POINTER (n-1));
						}
					}
				} else {
					g_hash_table_remove (matchhash, uid+8);
				}
			}
			camel_message_info_free ((CamelMessageInfo *)mi);
		}
	}
	if (last != -1)
		camel_folder_summary_remove_range (folder->summary, start, last);

	/* now matchhash contains any new uid's, add them, etc */
	if (rebuilded && !correlating) {
		camel_db_begin_transaction (folder->parent_store->cdb_w, NULL);

	}
	g_hash_table_foreach (matchhash, (GHFunc)folder_added_uid, &u);

	if (rebuilded && !correlating)
		camel_db_end_transaction (folder->parent_store->cdb_w, NULL);

	if (folder_unmatched != NULL) {
		/* scan unmatched, remove any that have vanished, etc */
		count = camel_folder_summary_count (((CamelFolder *)folder_unmatched)->summary);
		for (i=0;i<count;i++) {
			gchar *uid = camel_folder_summary_uid_from_index (((CamelFolder *)folder_unmatched)->summary, i);

			if (uid) {
				if (strncmp (uid, u.hash, 8) == 0) {
					if (g_hash_table_lookup (allhash, uid+8) == NULL) {
						/* no longer exists at all, just remove it entirely */
						camel_folder_summary_remove_index_fast (((CamelFolder *)folder_unmatched)->summary, i);
						camel_folder_change_info_remove_uid (folder_unmatched->changes, uid);
						i--;
					} else {
						g_hash_table_remove (allhash, uid+8);
					}
				}
				g_free (uid);
			}
		}

		/* now allhash contains all potentially new uid's for the unmatched folder, process */
		if (!CAMEL_IS_VEE_FOLDER (source))
			g_hash_table_foreach (allhash, (GHFunc)unmatched_check_uid, &u);

		/* copy any changes so we can raise them outside the lock */
		if (camel_folder_change_info_changed (folder_unmatched->changes)) {
			unmatched_changes = folder_unmatched->changes;
			folder_unmatched->changes = camel_folder_change_info_new ();
		}

		camel_vee_folder_unlock (folder_unmatched, CVF_SUMMARY_LOCK);
	}

	if (camel_folder_change_info_changed (vee_folder->changes)) {
		vee_folder_changes = vee_folder->changes;
		vee_folder->changes = camel_folder_change_info_new ();
	}

	camel_vee_folder_unlock (vee_folder, CVF_SUMMARY_LOCK);

	/* Del the unwanted things from the summary, we don't hold any locks now. */
	if (del_list) {
		if (!correlating) {
			CamelException ex = CAMEL_EXCEPTION_INITIALISER;
			camel_db_delete_vuids (
				folder->parent_store->cdb_w,
				folder->full_name, shash, del_list, &ex);
			camel_exception_clear (&ex);
		}
		((CamelVeeSummary *)folder->summary)->force_counts = TRUE;
		g_slist_foreach (del_list, (GFunc) camel_pstring_free, NULL);
		g_slist_free (del_list);
	};

	g_hash_table_destroy (matchhash);
	g_hash_table_destroy (allhash);
	g_hash_table_destroy (fullhash);

	g_free (shash);
	/* if expression not set, we only had a null list */
	if (vee_folder->expression == NULL || !rebuilded) {
		g_ptr_array_foreach (match, (GFunc) camel_pstring_free, NULL);
		g_ptr_array_free (match, TRUE);
	} else
		camel_folder_search_free (source, match);
	camel_folder_free_summary (source, all);

	if (unmatched_changes) {
		camel_object_trigger_event ((CamelObject *)folder_unmatched, "folder_changed", unmatched_changes);
		camel_folder_change_info_free (unmatched_changes);
	}

	if (vee_folder_changes) {
		camel_object_trigger_event ((CamelObject *)vee_folder, "folder_changed", vee_folder_changes);
		camel_folder_change_info_free (vee_folder_changes);
	}

	return 0;
}

static void
vee_folder_folder_changed (CamelVeeFolder *vee_folder,
                           CamelFolder *sub,
                           CamelFolderChangeInfo *changes)
{
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vee_folder);
	struct _folder_changed_msg *m;
	CamelSession *session = ((CamelService *)((CamelFolder *)vee_folder)->parent_store)->session;

	if (p->destroyed)
		return;

	m = camel_session_thread_msg_new (session, &folder_changed_ops, sizeof (*m));
	m->changes = camel_folder_change_info_new ();
	camel_folder_change_info_cat (m->changes, changes);
	m->sub = g_object_ref (sub);
	m->vee_folder = g_object_ref (vee_folder);
	camel_session_thread_queue (session, &m->msg, 0);
}

static void
vee_folder_folder_renamed (CamelVeeFolder *vee_folder,
                           CamelFolder *f,
                           const gchar *old)
{
	gchar hash[8];
	CamelVeeFolder *folder_unmatched = vee_folder->parent_vee_store ? vee_folder->parent_vee_store->folder_unmatched : NULL;

	/* TODO: This could probably be done in another thread, tho it is pretty quick/memory bound */

	/* Life just got that little bit harder, if the folder is renamed, it means it breaks all of our uid's.
	   We need to remove the old uid's, fix them up, then release the new uid's, for the uid's that match this folder */

	camel_vee_folder_hash_folder (f, hash);

	subfolder_renamed_update (vee_folder, f, hash);
	if (folder_unmatched != NULL)
		subfolder_renamed_update (folder_unmatched, f, hash);
}

static void
camel_vee_folder_class_init (CamelVeeFolderClass *class)
{
	GObjectClass *object_class;
	CamelObjectClass *camel_object_class;
	CamelFolderClass *folder_class;

	g_type_class_add_private (class, sizeof (CamelVeeFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = vee_folder_finalize;

	camel_object_class = CAMEL_OBJECT_CLASS (class);
	camel_object_class->getv = vee_folder_getv;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->refresh_info = vee_folder_refresh_info;
	folder_class->sync = vee_folder_sync;
	folder_class->expunge = vee_folder_expunge;
	folder_class->get_message = vee_folder_get_message;
	folder_class->append_message = vee_folder_append_message;
	folder_class->transfer_messages_to = vee_folder_transfer_messages_to;
	folder_class->search_by_expression = vee_folder_search_by_expression;
	folder_class->search_by_uids = vee_folder_search_by_uids;
	folder_class->count_by_expression = vee_folder_count_by_expression;
	folder_class->delete = vee_folder_delete;
	folder_class->freeze = vee_folder_freeze;
	folder_class->thaw = vee_folder_thaw;

	class->set_expression = vee_folder_set_expression;
	class->add_folder = vee_folder_add_folder;
	class->remove_folder = vee_folder_remove_folder;
	class->rebuild_folder = vee_folder_rebuild_folder;
	class->folder_changed = vee_folder_folder_changed;
	class->folder_renamed = vee_folder_folder_renamed;
}

static void
camel_vee_folder_init (CamelVeeFolder *vee_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (vee_folder);

	vee_folder->priv = CAMEL_VEE_FOLDER_GET_PRIVATE (vee_folder);

	folder->folder_flags |= (CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY |
				 CAMEL_FOLDER_HAS_SEARCH_CAPABILITY);

	/* FIXME: what to do about user flags if the subfolder doesn't support them? */
	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN;

	vee_folder->changes = camel_folder_change_info_new ();
	vee_folder->search = camel_folder_search_new ();
	vee_folder->hashes = g_hash_table_new_full (
		g_str_hash, g_str_equal, g_free, NULL);

	/* Loaded is no longer used.*/
	vee_folder->loaded = NULL;
	vee_folder->deleted = FALSE;
	vee_folder->priv->summary_lock = g_mutex_new ();
	vee_folder->priv->subfolder_lock = g_mutex_new ();
	vee_folder->priv->changed_lock = g_mutex_new ();
	vee_folder->priv->unread_vfolder = -1;
}

void
camel_vee_folder_construct (CamelVeeFolder *vf, CamelStore *parent_store, const gchar *full, const gchar *name, guint32 flags)
{
	CamelFolder *folder = (CamelFolder *)vf;

	vf->flags = flags;
	camel_folder_construct (folder, parent_store, full, name);

	folder->summary = camel_vee_summary_new (folder);

	if (CAMEL_IS_VEE_STORE (parent_store))
		vf->parent_vee_store = (CamelVeeStore *)parent_store;
}

/**
 * camel_vee_folder_new:
 * @parent_store: the parent CamelVeeStore
 * @full: the full path to the vfolder.
 * @flags: flags of some kind
 *
 * Create a new CamelVeeFolder object.
 *
 * Returns: A new CamelVeeFolder widget.
 **/
CamelFolder *
camel_vee_folder_new (CamelStore *parent_store, const gchar *full, guint32 flags)
{
	CamelVeeFolder *vf;
	gchar *tmp;

	if (CAMEL_IS_VEE_STORE (parent_store) && strcmp (full, CAMEL_UNMATCHED_NAME) == 0) {
		vf = ((CamelVeeStore *)parent_store)->folder_unmatched;
		g_object_ref (vf);
	} else {
		const gchar *name = strrchr (full, '/');

		if (name == NULL)
			name = full;
		else
			name++;
		vf = g_object_new (CAMEL_TYPE_VEE_FOLDER, NULL);
		camel_vee_folder_construct (vf, parent_store, full, name, flags);
	}

	d (printf ("returning folder %s %p, count = %d\n", full, vf, camel_folder_get_message_count ((CamelFolder *)vf)));

	if (vf) {
		tmp = g_strdup_printf ("%s/%s.cmeta", ((CamelService *)parent_store)->url->path, full);
		camel_object_set (vf, NULL, CAMEL_OBJECT_STATE_FILE, tmp, NULL);
		g_free (tmp);
		if (camel_object_state_read (vf) == -1) {
			/* setup defaults: we have none currently */
		}
	}
	return (CamelFolder *)vf;
}

void
camel_vee_folder_set_expression (CamelVeeFolder *vf, const gchar *query)
{
	CAMEL_VEE_FOLDER_GET_CLASS (vf)->set_expression (vf, query);
}

/**
 * camel_vee_folder_add_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to add to @vf
 *
 * Adds @sub as a source folder to @vf.
 **/
void
camel_vee_folder_add_folder (CamelVeeFolder *vf, CamelFolder *sub)
{
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vf);
	gint i;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	if (vf == (CamelVeeFolder *)sub) {
		g_warning ("Adding a virtual folder to itself as source, ignored");
		return;
	}

	camel_vee_folder_lock (vf, CVF_SUBFOLDER_LOCK);

	/* for normal vfolders we want only unique ones, for unmatched we want them all recorded */
	if (g_list_find (p->folders, sub) == NULL) {
		p->folders = g_list_append (
			p->folders, g_object_ref (sub));

		camel_folder_lock (CAMEL_FOLDER (vf), CF_CHANGE_LOCK);

		/* update the freeze state of 'sub' to match our freeze state */
		for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *)vf); i++)
			camel_folder_freeze (sub);

		camel_folder_unlock (CAMEL_FOLDER (vf), CF_CHANGE_LOCK);
	}
	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !CAMEL_IS_VEE_FOLDER (sub) && folder_unmatched != NULL) {
		CamelVeeFolderPrivate *up = CAMEL_VEE_FOLDER_GET_PRIVATE (folder_unmatched);
		up->folders = g_list_append (
			up->folders, g_object_ref (sub));

		camel_folder_lock (CAMEL_FOLDER (folder_unmatched), CF_CHANGE_LOCK);

		/* update the freeze state of 'sub' to match Unmatched's freeze state */
		for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *)folder_unmatched); i++)
			camel_folder_freeze (sub);

		camel_folder_unlock (CAMEL_FOLDER (folder_unmatched), CF_CHANGE_LOCK);
	}

	camel_vee_folder_unlock (vf, CVF_SUBFOLDER_LOCK);

	d (printf ("camel_vee_folder_add_folder (%s, %s)\n", ((CamelFolder *)vf)->full_name, sub->full_name));

	camel_object_hook_event ((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc)folder_changed, vf);
	camel_object_hook_event ((CamelObject *)sub, "deleted", (CamelObjectEventHookFunc)subfolder_deleted, vf);
	camel_object_hook_event ((CamelObject *)sub, "renamed", (CamelObjectEventHookFunc)folder_renamed, vf);

	CAMEL_VEE_FOLDER_GET_CLASS (vf)->add_folder (vf, sub);
}

/**
 * camel_vee_folder_remove_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to remove from @vf
 *
 * Removed the source folder, @sub, from the virtual folder, @vf.
 **/
void
camel_vee_folder_remove_folder (CamelVeeFolder *vf, CamelFolder *sub)
{
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vf);
	gint i;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	camel_vee_folder_lock (vf, CVF_SUBFOLDER_LOCK);

	camel_vee_folder_lock (vf, CVF_CHANGED_LOCK);
	p->folders_changed = g_list_remove (p->folders_changed, sub);
	camel_vee_folder_unlock (vf, CVF_CHANGED_LOCK);

	if (g_list_find (p->folders, sub) == NULL) {
		camel_vee_folder_unlock (vf, CVF_SUBFOLDER_LOCK);
		return;
	}

	camel_object_unhook_event ((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc) folder_changed, vf);
	camel_object_unhook_event ((CamelObject *)sub, "deleted", (CamelObjectEventHookFunc) subfolder_deleted, vf);
	camel_object_unhook_event ((CamelObject *)sub, "renamed", (CamelObjectEventHookFunc) folder_renamed, vf);

	p->folders = g_list_remove (p->folders, sub);

	/* undo the freeze state that we have imposed on this source folder */
	camel_folder_lock (CAMEL_FOLDER (vf), CF_CHANGE_LOCK);
	for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *)vf); i++)
		camel_folder_thaw (sub);
	camel_folder_unlock (CAMEL_FOLDER (vf), CF_CHANGE_LOCK);

	camel_vee_folder_unlock (vf, CVF_SUBFOLDER_LOCK);

	if (folder_unmatched != NULL) {
		CamelVeeFolderPrivate *up = CAMEL_VEE_FOLDER_GET_PRIVATE (folder_unmatched);

		camel_vee_folder_lock (folder_unmatched, CVF_SUBFOLDER_LOCK);
		/* if folder deleted, then blow it away from unmatched always, and remove all refs to it */
		if (sub->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED) {
			while (g_list_find (up->folders, sub)) {
				up->folders = g_list_remove (up->folders, sub);
				g_object_unref (sub);

				/* undo the freeze state that Unmatched has imposed on this source folder */
				camel_folder_lock (CAMEL_FOLDER (folder_unmatched), CF_CHANGE_LOCK);
				for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *)folder_unmatched); i++)
					camel_folder_thaw (sub);
				camel_folder_unlock (CAMEL_FOLDER (folder_unmatched), CF_CHANGE_LOCK);
			}
		} else if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
			if (g_list_find (up->folders, sub) != NULL) {
				up->folders = g_list_remove (up->folders, sub);
				g_object_unref (sub);

				/* undo the freeze state that Unmatched has imposed on this source folder */
				camel_folder_lock (CAMEL_FOLDER (folder_unmatched), CF_CHANGE_LOCK);
				for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *)folder_unmatched); i++)
					camel_folder_thaw (sub);
				camel_folder_unlock (CAMEL_FOLDER (folder_unmatched), CF_CHANGE_LOCK);
			}
		}
		camel_vee_folder_unlock (folder_unmatched, CVF_SUBFOLDER_LOCK);
	}

	CAMEL_VEE_FOLDER_GET_CLASS (vf)->remove_folder (vf, sub);

	if (CAMEL_IS_VEE_FOLDER (sub))
		return;

	g_object_unref (sub);
}

/**
 * camel_vee_folder_rebuild_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to add to @vf
 * @ex: Exception.
 *
 * Rebuild the folder @sub, if it should be.
 **/
gint
camel_vee_folder_rebuild_folder (CamelVeeFolder *vf,
                                 CamelFolder *sub,
                                 CamelException *ex)
{
	return CAMEL_VEE_FOLDER_GET_CLASS (vf)->rebuild_folder (vf, sub, ex);
}

static void
remove_folders (CamelFolder *folder, CamelFolder *foldercopy, CamelVeeFolder *vf)
{
	camel_vee_folder_remove_folder (vf, folder);
	g_object_unref (folder);
}

/**
 * camel_vee_folder_set_folders:
 * @vf:
 * @folders:
 *
 * Set the whole list of folder sources on a vee folder.
 **/
void
camel_vee_folder_set_folders (CamelVeeFolder *vf, GList *folders)
{
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER_GET_PRIVATE (vf);
	GHashTable *remove = g_hash_table_new (NULL, NULL);
	GList *l;
	CamelFolder *folder;

	/* setup a table of all folders we have currently */
	camel_vee_folder_lock (vf, CVF_SUBFOLDER_LOCK);
	l = p->folders;
	while (l) {
		g_hash_table_insert (remove, l->data, l->data);
		g_object_ref (l->data);
		l = l->next;
	}
	camel_vee_folder_unlock (vf, CVF_SUBFOLDER_LOCK);

	/* if we already have the folder, ignore it, otherwise add it */
	l = folders;
	while (l) {
		if ((folder = g_hash_table_lookup (remove, l->data))) {
			g_hash_table_remove (remove, folder);
			g_object_unref (folder);
		} else {
			camel_vee_folder_add_folder (vf, l->data);
		}
		l = l->next;
	}

	/* then remove any we still have */
	g_hash_table_foreach (remove, (GHFunc)remove_folders, vf);
	g_hash_table_destroy (remove);
}

/**
 * camel_vee_folder_hash_folder:
 * @folder:
 * @:
 *
 * Create a hash string representing the folder name, which should be
 * unique, and remain static for a given folder.
 **/
void
camel_vee_folder_hash_folder (CamelFolder *folder, gchar buffer[8])
{
	GChecksum *checksum;
	guint8 *digest;
	gsize length;
	gint state = 0, save = 0;
	gchar *tmp;
	gint i;

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	tmp = camel_service_get_url ((CamelService *)folder->parent_store);
	g_checksum_update (checksum, (guchar *) tmp, -1);
	g_free (tmp);
	tmp = folder->full_name;
	g_checksum_update (checksum, (guchar *) tmp, -1);
	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	g_base64_encode_step (digest, 6, FALSE, buffer, &state, &save);
	g_base64_encode_close (FALSE, buffer, &state, &save);

	for (i=0;i<8;i++) {
		if (buffer[i] == '+')
			buffer[i] = '.';
		if (buffer[i] == '/')
			buffer[i] = '_';
	}
}

/**
 * camel_vee_folder_get_location:
 * @vf:
 * @vinfo:
 * @realuid: if not NULL, set to the uid of the real message, must be
 * g_free'd by caller.
 *
 * Find the real folder (and uid)
 *
 * Returns:
 **/
CamelFolder *
camel_vee_folder_get_location (CamelVeeFolder *vf, const CamelVeeMessageInfo *vinfo, gchar **realuid)
{
	CamelFolder *folder;

	folder = vinfo->summary->folder;

	/* locking?  yes?  no?  although the vfolderinfo is valid when obtained
	   the folder in it might not necessarily be so ...? */
	if (CAMEL_IS_VEE_FOLDER (folder)) {
		CamelFolder *res;
		const CamelVeeMessageInfo *vfinfo;

		vfinfo = (CamelVeeMessageInfo *)camel_folder_get_message_info (folder, camel_message_info_uid (vinfo)+8);
		res = camel_vee_folder_get_location ((CamelVeeFolder *)folder, vfinfo, realuid);
		camel_folder_free_message_info (folder, (CamelMessageInfo *)vfinfo);
		return res;
	} else {
		if (realuid)
			*realuid = g_strdup (camel_message_info_uid (vinfo)+8);

		return folder;
	}
}

/**
 * camel_vee_folder_mask_event_folder_changed:
 *
 * Since: 2.26
 **/
void
camel_vee_folder_mask_event_folder_changed (CamelVeeFolder *vf, CamelFolder *sub)
{
	camel_object_unhook_event ((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc) folder_changed, vf);

}

/**
 * camel_vee_folder_unmask_event_folder_changed:
 *
 * Since: 2.26
 **/
void
camel_vee_folder_unmask_event_folder_changed (CamelVeeFolder *vf, CamelFolder *sub)
{
	camel_object_hook_event ((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc) folder_changed, vf);
}

/**
 * camel_vee_folder_sync_headers:
 *
 * Since: 2.24
 **/
void
camel_vee_folder_sync_headers (CamelFolder *vf,
                               CamelException *ex)
{
	CamelFIRecord * record;
	time_t start, end;

	/* Save the counts to DB */
	start = time (NULL);
	record = summary_header_to_db (vf->summary, ex);
	camel_db_write_folder_info_record (vf->parent_store->cdb_w, record, ex);
	end = time (NULL);
	dd (printf ("Sync for vfolder '%s': %ld secs\n", vf->full_name, end-start));

	g_free (record);
}

/* FIXME: This shouldn't be needed */
gint
camel_vee_folder_get_unread_vfolder (CamelVeeFolder *folder)
{
	g_return_val_if_fail (folder != NULL, 0);
	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (folder), 0);

	return folder->priv->unread_vfolder;
}

/* FIXME: This shouldn't be needed */
void
camel_vee_folder_set_unread_vfolder (CamelVeeFolder *folder,
                                     gint unread_vfolder)
{
	g_return_if_fail (folder != NULL);
	g_return_if_fail (CAMEL_IS_VEE_FOLDER (folder));

	folder->priv->unread_vfolder = unread_vfolder;
}

/**
 * camel_vee_folder_lock:
 * @folder: a #CamelVeeFolder
 * @lock: lock type to lock
 *
 * Locks #folder's #lock. Unlock it with camel_vee_folder_unlock().
 *
 * Since: 3.0
 **/
void
camel_vee_folder_lock (CamelVeeFolder *folder,
                       CamelVeeFolderLock lock)
{
	g_return_if_fail (CAMEL_IS_VEE_FOLDER (folder));

	switch (lock) {
		case CVF_SUMMARY_LOCK:
			g_mutex_lock (folder->priv->summary_lock);
			break;
		case CVF_SUBFOLDER_LOCK:
			g_mutex_lock (folder->priv->subfolder_lock);
			break;
		case CVF_CHANGED_LOCK:
			g_mutex_lock (folder->priv->changed_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_vee_folder_unlock:
 * @folder: a #CamelVeeFolder
 * @lock: lock type to unlock
 *
 * Unlocks #folder's #lock, previously locked with camel_vee_folder_lock().
 *
 * Since: 3.0
 **/
void
camel_vee_folder_unlock (CamelVeeFolder *folder,
                         CamelVeeFolderLock lock)
{
	g_return_if_fail (CAMEL_IS_VEE_FOLDER (folder));

	switch (lock) {
		case CVF_SUMMARY_LOCK:
			g_mutex_unlock (folder->priv->summary_lock);
			break;
		case CVF_SUBFOLDER_LOCK:
			g_mutex_unlock (folder->priv->subfolder_lock);
			break;
		case CVF_CHANGED_LOCK:
			g_mutex_unlock (folder->priv->changed_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

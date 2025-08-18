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
 *          Jeffrey Stedfast <fejj@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-debug.h"
#include "camel-mime-message.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-store-search.h"
#include "camel-vee-folder.h"
#include "camel-vee-store.h"	/* for open flags */
#include "camel-vee-summary.h"
#include "camel-string-utils.h"
#include "camel-vtrash-folder.h"

#define d(x)
#define dd(x) (camel_debug ("vfolder")?(x):0)

extern gint camel_application_is_exiting;

struct _CamelVeeFolderPrivate {
	guint32 flags;		/* folder open flags */
	gboolean destroyed;
	GHashTable *subfolders;		/* (CamelFolder * ~> NULL); lock using subfolder_lock before changing/accessing */
	gboolean auto_update;

	GRecMutex subfolder_lock;	/* for locking the subfolder list */
	GRecMutex changed_lock;		/* for locking the folders-changed list and rebuild schedule */

	GHashTable *real_subfolders; /* (const gchar *sfid ~> SubfolderData *); used real folders, filled by rebuild() */
	GCancellable *rebuild_cancellable;

	gchar *expression;	/* query expression */
};

enum {
	PROP_0,
	PROP_AUTO_UPDATE,
	N_PROPS
};

enum {
	VEE_SETUP_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static GParamSpec *properties[N_PROPS] = { NULL, };

/* signals only for testing purposes */
#ifdef ENABLE_MAINTAINER_MODE
enum {
	REBUILD_SCHEDULE_TEST_SIGNAL = LAST_SIGNAL,
	REBUILD_RUN_TEST_SIGNAL,
	LAST_TEST_SIGNAL
};
static guint test_signals[LAST_TEST_SIGNAL];
#endif

G_DEFINE_TYPE_WITH_PRIVATE (CamelVeeFolder, camel_vee_folder, CAMEL_TYPE_FOLDER)

static void
vee_folder_create_subfolder_id (CamelFolder *subfolder,
				gchar buffer[9])
{
	CamelStore *parent_store;
	GChecksum *checksum;
	guint8 *digest;
	gsize length;
	gint state = 0, save = 0;
	const gchar *uid;
	gint i;

	/* only for real folders */
	g_warn_if_fail (!CAMEL_IS_VEE_FOLDER (subfolder));

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	parent_store = camel_folder_get_parent_store (subfolder);
	uid = camel_service_get_uid (CAMEL_SERVICE (parent_store));

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, (guchar *) uid, -1);
	g_checksum_update (checksum, (guchar *) camel_folder_get_full_name (subfolder), -1);

	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	g_base64_encode_step (digest, 6, FALSE, buffer, &state, &save);
	g_base64_encode_close (FALSE, buffer, &state, &save);

	for (i = 0; i < 8; i++) {
		if (buffer[i] == '+')
			buffer[i] = '.';
		if (buffer[i] == '/')
			buffer[i] = '_';
	}

	buffer[8] = '\0';
}

typedef struct _VeeUidBuilder {
	const gchar *subfolder_id;
	gchar *buffer;
	guint buffer_len;
	guint subfolder_id_len;
} VeeUidBuilder;

static void
vee_uid_builder_init (VeeUidBuilder *self,
		      const gchar *subfolder_id)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (subfolder_id != NULL);

	self->subfolder_id_len = strlen (subfolder_id);

	/* preallocate some (16) bytes, it will increase its size if needed */
	self->buffer_len = self->subfolder_id_len + 16 + 1;
	self->buffer = g_malloc (self->buffer_len);
	strcpy (self->buffer, subfolder_id);
}

static void
vee_uid_builder_clear (VeeUidBuilder *self)
{
	g_free (self->buffer);
	memset (self, 0, sizeof (VeeUidBuilder));
}

static const gchar * /* (transfer none) */
vee_uid_builder_get (VeeUidBuilder *self,
		     const gchar *subf_uid)
{
	guint uid_len = strlen (subf_uid);
	const gchar *vuid;

	if (self->subfolder_id_len + uid_len + 1 > self->buffer_len) {
		self->buffer_len = self->subfolder_id_len + uid_len + 1;
		self->buffer = g_realloc (self->buffer, self->buffer_len);
	}

	strcpy (self->buffer + self->subfolder_id_len, subf_uid);

	/* this saves couple cycles with the string pool, expecting the vuid being
	   in the pool when it's included in this folder, thus it cannot be freed
	   earlier than when it's removed from the vfolder's summary; all the callers
	   to this function should ensure it */
	vuid = camel_pstring_peek (self->buffer);
	if (!vuid)
		vuid = self->buffer;

	return vuid;
}

static void
vee_folder_emit_setup_changed (CamelVeeFolder *self)
{
	g_signal_emit (self, signals[VEE_SETUP_CHANGED], 0, NULL);
}

typedef struct _RebuildData {
	CamelVeeFolder *self;
	gboolean emit_setup_changed;
} RebuildData;

static RebuildData *
rebuild_data_new (CamelVeeFolder *self,
		  gboolean emit_setup_changed)
{
	RebuildData *rd;

	rd = g_new0 (RebuildData, 1);
	rd->self = g_object_ref (self);
	rd->emit_setup_changed = emit_setup_changed;

	return rd;
}

static void
rebuild_data_free (gpointer ptr)
{
	RebuildData *rd = ptr;

	if (rd) {
		g_clear_object (&rd->self);
		g_free (rd);
	}
}

static gboolean
vee_folder_rebuild_sync (CamelVeeFolder *vfolder,
			 GCancellable *cancellable,
			 GError **error);

static void
vee_folder_rebuild_job_cb (CamelSession *session,
			   GCancellable *cancellable,
			   gpointer user_data,
			   GError **error)
{
	RebuildData *rd = user_data;
	CamelVeeFolder *self = rd->self;

	g_rec_mutex_lock (&self->priv->changed_lock);
	if (self->priv->rebuild_cancellable) {
		g_cancellable_cancel (self->priv->rebuild_cancellable);
		g_clear_object (&self->priv->rebuild_cancellable);
	}

	self->priv->rebuild_cancellable = camel_operation_new_proxy (cancellable);
	cancellable = g_object_ref (self->priv->rebuild_cancellable);
	g_rec_mutex_unlock (&self->priv->changed_lock);

	vee_folder_rebuild_sync (self, cancellable, error);

	g_rec_mutex_lock (&self->priv->changed_lock);
	if (self->priv->rebuild_cancellable == cancellable)
		g_clear_object (&self->priv->rebuild_cancellable);
	g_rec_mutex_unlock (&self->priv->changed_lock);

	g_clear_object (&cancellable);

	if (rd->emit_setup_changed)
		vee_folder_emit_setup_changed (self);
}

static void
vee_folder_schedule_rebuild (CamelVeeFolder *self,
			     gboolean emit_setup_changed)
{
	CamelFolder *folder;
	CamelStore *parent_store;
	CamelSession *session;
	gchar *description;

	if (self->priv->destroyed)
		return;

	folder = CAMEL_FOLDER (self);
	parent_store = camel_folder_get_parent_store (folder);
	if (!parent_store)
		return;

	session = camel_service_ref_session (CAMEL_SERVICE (parent_store));
	if (!session)
		return;

	g_rec_mutex_lock (&self->priv->changed_lock);

	#ifdef ENABLE_MAINTAINER_MODE
	g_signal_emit (self, test_signals[REBUILD_SCHEDULE_TEST_SIGNAL], 0, NULL);
	#endif

	if (self->priv->rebuild_cancellable) {
		g_cancellable_cancel (self->priv->rebuild_cancellable);
		g_clear_object (&self->priv->rebuild_cancellable);
	}

	description = g_strdup_printf (_("Updating search folder “%s”"), camel_folder_get_full_display_name (folder));

	camel_session_submit_job (session, description, vee_folder_rebuild_job_cb,
		rebuild_data_new (self, emit_setup_changed), rebuild_data_free);

	g_free (description);
	g_object_unref (session);

	g_rec_mutex_unlock (&self->priv->changed_lock);
}

static void
vee_folder_subfolder_vee_setup_changed_cb (CamelVeeFolder *subfolder,
					   gpointer user_data)
{
	CamelVeeFolder *self = user_data;

	/* emit after rebuild, because some parent vFolders may expect up-to-date
	   content of its subfolders (like for the match-threads search) */
	if (self->priv->auto_update && self->priv->expression)
		vee_folder_schedule_rebuild (self, TRUE);
	else
		vee_folder_emit_setup_changed (self);
}

typedef struct _SubfolderData {
	CamelVeeFolder *self;
	CamelFolder *folder;
	gchar *subfolder_id;
	gulong changed_handler_id;
	gulong summary_info_flags_changed_id;
} SubfolderData;

static void
vee_folder_subfolder_changed_cb (CamelFolder *subfolder,
				 CamelFolderChangeInfo *sub_changes,
				 gpointer user_data)
{
	SubfolderData *sd = user_data;
	CamelFolderChangeInfo *changes;
	CamelFolderSummary *summary;
	CamelVeeSummary *vsummary;
	VeeUidBuilder vuid_builder = { 0, };
	gboolean schedule_rebuild = FALSE;
	guint ii;

	g_return_if_fail (sd != NULL);
	g_return_if_fail (sub_changes != NULL);

	if (sd->self->priv->auto_update && sub_changes->uid_added && sub_changes->uid_added->len) {
		vee_folder_schedule_rebuild (sd->self, FALSE);
		return;
	}

	vee_uid_builder_init (&vuid_builder, sd->subfolder_id);

	summary = camel_folder_get_folder_summary (CAMEL_FOLDER (sd->self));
	vsummary = CAMEL_VEE_SUMMARY (summary);
	changes = camel_folder_change_info_new ();

	if (sub_changes->uid_removed) {
		for (ii = 0; ii < sub_changes->uid_removed->len; ii++) {
			const gchar *uid = g_ptr_array_index (sub_changes->uid_removed, ii);
			const gchar *vuid;

			vuid = vee_uid_builder_get (&vuid_builder, uid);
			if (camel_folder_summary_check_uid (summary, vuid)) {
				camel_folder_change_info_remove_uid (changes, vuid);
				camel_vee_summary_remove (vsummary, subfolder, vuid);
			}
		}
	}

	if (sub_changes->uid_changed) {
		for (ii = 0; ii < sub_changes->uid_changed->len; ii++) {
			const gchar *uid = g_ptr_array_index (sub_changes->uid_changed, ii);
			const gchar *vuid;

			vuid = vee_uid_builder_get (&vuid_builder, uid);
			if (camel_folder_summary_check_uid (summary, vuid)) {
				camel_folder_change_info_change_uid (changes, vuid);
				camel_vee_summary_replace_flags (vsummary, vuid);
			} else {
				/* something not in the folder changed; maybe it can be added to the folder,
				   but it's not known until the rebuild, thus flag to schedule it */
				schedule_rebuild = TRUE;
			}
		}
	}

	if (sub_changes->uid_recent) {
		for (ii = 0; ii < sub_changes->uid_recent->len; ii++) {
			const gchar *uid = g_ptr_array_index (sub_changes->uid_recent, ii);
			const gchar *vuid;

			vuid = vee_uid_builder_get (&vuid_builder, uid);
			if (camel_folder_summary_check_uid (summary, vuid)) {
				camel_folder_change_info_recent_uid (changes, vuid);
			}
		}
	}

	vee_uid_builder_clear (&vuid_builder);

	if (camel_folder_change_info_changed (changes))
		camel_folder_changed (CAMEL_FOLDER (sd->self), changes);

	if (sd->self->priv->auto_update && schedule_rebuild)
		vee_folder_schedule_rebuild (sd->self, FALSE);

	camel_folder_change_info_free (changes);
}

static void
vee_folder_subfolder_summary_info_flags_changed_cb (CamelFolderSummary *summary,
						    const gchar *uid,
						    guint new_flags,
						    gpointer user_data)
{
	SubfolderData *sd = user_data;
	CamelFolderSummary *my_summary;
	gchar *vuid;

	g_return_if_fail (sd != NULL);
	g_return_if_fail (uid != NULL);

	my_summary = camel_folder_get_folder_summary (CAMEL_FOLDER (sd->self));

	vuid = g_strconcat (sd->subfolder_id, uid, NULL);

	/* the function checks for the existence on its own, no need to double-check */
	camel_folder_summary_replace_flags (my_summary, vuid, new_flags);

	g_free (vuid);
}

static SubfolderData *
subfolder_data_new (CamelVeeFolder *self,
		    CamelFolder *folder,
		    const gchar *subfolder_id)
{
	SubfolderData *sd;
	CamelFolderSummary *summary;

	summary = camel_folder_get_folder_summary (folder);

	sd = g_new0 (SubfolderData, 1);
	sd->self = self;
	sd->folder = g_object_ref (folder);
	sd->subfolder_id = g_strdup (subfolder_id);
	sd->changed_handler_id = g_signal_connect (folder, "changed", G_CALLBACK (vee_folder_subfolder_changed_cb), sd);

	if (summary) {
		sd->summary_info_flags_changed_id = g_signal_connect (summary, "info-flags-changed",
			G_CALLBACK (vee_folder_subfolder_summary_info_flags_changed_cb), sd);
	}

	return sd;
}

static void
subfolder_data_free (gpointer ptr)
{
	SubfolderData *sd = ptr;

	if (sd) {
		if (sd->changed_handler_id)
			g_signal_handler_disconnect (sd->folder, sd->changed_handler_id);
		if (sd->summary_info_flags_changed_id)
			g_signal_handler_disconnect (camel_folder_get_folder_summary (sd->folder), sd->summary_info_flags_changed_id);
		g_clear_object (&sd->folder);
		g_free (sd->subfolder_id);
		g_free (sd);
	}
}

static gboolean
vee_folder_get_expression_is_match_threads (CamelVeeFolder *vfolder,
					    const gchar *expression)
{
	CamelStoreSearch *search;
	gboolean is_match_threads = FALSE;

	if (!expression || !*expression)
		return FALSE;

	search = camel_store_search_new (camel_folder_get_parent_store (CAMEL_FOLDER (vfolder)));
	camel_store_search_set_expression (search, expression);
	if (camel_store_search_rebuild_sync (search, NULL, NULL)) {
		CamelMatchThreadsKind kind;
		CamelFolderThreadFlags flags = CAMEL_FOLDER_THREAD_FLAG_NONE;

		kind = camel_store_search_get_match_threads_kind (search, &flags);
		is_match_threads = kind != CAMEL_MATCH_THREADS_KIND_NONE;
	}

	g_clear_object (&search);

	return is_match_threads;
}

static void
vee_folder_claim_subfolder_uids (CamelVeeFolder *vfolder,
				 CamelFolder *subfolder,
				 const gchar *subfolder_id,
				 GPtrArray *uids,
				 CamelFolderChangeInfo *changes)
{
	CamelVeeSummary *vsummary;
	GHashTable *current_uids;

	vsummary = CAMEL_VEE_SUMMARY (camel_folder_get_folder_summary (CAMEL_FOLDER (vfolder)));
	current_uids = camel_vee_summary_get_uids_for_subfolder (vsummary, subfolder);

	if (uids) {
		VeeUidBuilder vuid_builder = { 0, };
		guint ii;

		vee_uid_builder_init (&vuid_builder, subfolder_id);

		for (ii = 0; ii < uids->len; ii++) {
			const gchar *subf_uid = g_ptr_array_index (uids, ii);
			const gchar *vuid;

			if (!subf_uid)
				continue;

			vuid = vee_uid_builder_get (&vuid_builder, subf_uid);
			if (!current_uids || !g_hash_table_remove (current_uids, vuid)) {
				CamelVeeMessageInfo *vmi;

				vmi = camel_vee_summary_add (vsummary, subfolder, vuid);
				if (vmi) {
					camel_folder_change_info_add_uid (changes, vuid);
					g_clear_object (&vmi);
				}
			} else if (camel_vee_summary_replace_flags (vsummary, vuid)) {
				camel_folder_change_info_change_uid (changes, vuid);
			}
		}

		vee_uid_builder_clear (&vuid_builder);
	}

	if (current_uids) {
		GHashTableIter iter;
		gpointer key = NULL;

		g_hash_table_iter_init (&iter, current_uids);
		while (g_hash_table_iter_next (&iter, &key, NULL)) {
			const gchar *vuid = key;

			camel_folder_change_info_remove_uid (changes, vuid);
			camel_vee_summary_remove (vsummary, subfolder, vuid);
		}
	}

	g_clear_pointer (&current_uids, g_hash_table_unref);
}

typedef struct _SearchData {
	GHashTable *by_store; /* CamelStore * ~> GHashTable * { gchar *exprs ~> GHashTable * { CamelFolder *, NULL } } */
	GHashTable *subfolder_ids; /* CamelFolder * ~> gchar *subfolder_id */
	GPtrArray *match_indexes; /* CamelStoreSearchindex *; replacements for match-threads expressions */
} SearchData;

static SearchData *
search_data_new (void)
{
	SearchData *sd;

	sd = g_new0 (SearchData, 1);
	sd->by_store = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, (GDestroyNotify) g_hash_table_unref);
	sd->subfolder_ids = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	sd->match_indexes = NULL; /* created on demand, because it might not be usually needed */

	return sd;
}

static void
search_data_free (gpointer ptr)
{
	SearchData *sd = ptr;

	if (sd) {
		g_clear_pointer (&sd->by_store, g_hash_table_destroy);
		g_clear_pointer (&sd->subfolder_ids, g_hash_table_unref);
		g_clear_pointer (&sd->match_indexes, g_ptr_array_unref);
		g_free (sd);
	}
}

static gboolean
vee_folder_can_use_expr (const gchar *expr)
{
	return expr && *expr && g_strcmp0 (expr, "#t") != 0;
}

static void
vee_folder_fill_search_data (CamelVeeFolder *self,
			     SearchData *sd,
			     const gchar *top_expr,
			     gboolean with_subexpr)
{
	GHashTableIter iter;
	gpointer key = NULL;

	g_rec_mutex_lock (&self->priv->subfolder_lock);

	g_hash_table_iter_init (&iter, self->priv->subfolders);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		CamelFolder *subfolder = key;

		if (CAMEL_IS_VEE_FOLDER (subfolder)) {
			CamelVeeFolder *vfolder = CAMEL_VEE_FOLDER (subfolder);
			gchar *alloc_vee_expr = NULL;
			const gchar *vee_expr = NULL;
			gchar *alloc_expr = NULL;
			const gchar *expr = NULL;
			gboolean uses_match_index = FALSE;

			if (with_subexpr) {
				if (vee_folder_can_use_expr (vfolder->priv->expression)) {
					vee_expr = vfolder->priv->expression;

					if (vee_folder_get_expression_is_match_threads (vfolder, vee_expr)) {
						CamelFolderSummary *summary;
						CamelStoreSearchIndex *match_index;

						summary = camel_folder_get_folder_summary (CAMEL_FOLDER (vfolder));
						match_index = camel_vee_summary_to_match_index (CAMEL_VEE_SUMMARY (summary));
						if (!sd->match_indexes)
							sd->match_indexes = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_store_search_index_unref);
						g_ptr_array_add (sd->match_indexes, match_index);
						alloc_vee_expr = g_strdup_printf ("(in-match-index \"%p\")", match_index);
						vee_expr = alloc_vee_expr;
						uses_match_index = TRUE;
					}
				}

				if (vee_expr && vee_folder_can_use_expr (top_expr)) {
					alloc_expr = g_strconcat ("(and ", top_expr, " ", vee_expr, ")", NULL);
					expr = alloc_expr;
				} else if (vee_folder_can_use_expr (vee_expr)) {
					expr = vee_expr;
				} else {
					expr = top_expr;
				}
			} else {
				expr = top_expr;
			}

			vee_folder_fill_search_data (vfolder, sd, expr, with_subexpr && !uses_match_index);

			g_free (alloc_expr);
			g_free (alloc_vee_expr);
		} else {
			CamelStore *store = camel_folder_get_parent_store (subfolder);
			GHashTable *by_expr;
			GHashTable *folders;

			if (!store)
				continue;

			by_expr = g_hash_table_lookup (sd->by_store, store);
			if (!by_expr) {
				by_expr = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_hash_table_unref);
				g_hash_table_insert (sd->by_store, g_object_ref (store), by_expr);
			}

			folders = g_hash_table_lookup (by_expr, top_expr);
			if (!folders) {
				folders = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, NULL);
				g_hash_table_insert (by_expr, g_strdup (top_expr), folders);
			}

			g_hash_table_add (folders, g_object_ref (subfolder));

			if (!g_hash_table_contains (sd->subfolder_ids, subfolder)) {
				gchar buffer[9] = { 0, };

				vee_folder_create_subfolder_id (subfolder, buffer);
				g_hash_table_insert (sd->subfolder_ids, subfolder, g_strdup (buffer));
			}
		}
	}

	g_rec_mutex_unlock (&self->priv->subfolder_lock);
}

static gboolean
vee_folder_rebuild_for_expression_sync (CamelVeeFolder *vfolder,
					const gchar *expression,
					GCancellable *cancellable,
					GError **error)
{
	CamelFolderChangeInfo *changes;
	CamelMatchThreadsKind threads_kind = CAMEL_MATCH_THREADS_KIND_NONE;
	CamelFolderThreadFlags threads_flags = CAMEL_FOLDER_THREAD_FLAG_NONE;
	CamelStoreSearchIndex *search_index = NULL;
	GPtrArray *searches; /* CamelStoreSearch * */
	GPtrArray *items = NULL;
	GHashTable *current_subfolders;
	GHashTableIter iter;
	SearchData *sd;
	guint ii;
	gpointer key = NULL, value = NULL;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vfolder), FALSE);

	if (!expression || !*expression)
		return TRUE;

	changes = camel_folder_change_info_new ();

	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);

	current_subfolders = camel_vee_summary_dup_subfolders (CAMEL_VEE_SUMMARY (camel_folder_get_folder_summary (CAMEL_FOLDER (vfolder))));
	g_hash_table_remove_all (vfolder->priv->real_subfolders);

	sd = search_data_new ();
	vee_folder_fill_search_data (vfolder, sd, expression, TRUE);

	/* there can be more items needed, it's when different expressions are involved */
	searches = g_ptr_array_new_full (g_hash_table_size (sd->by_store), g_object_unref);

	g_hash_table_iter_init (&iter, sd->by_store);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		CamelStore *store = key;
		GHashTable *by_expr = value;
		GHashTableIter eiter;

		g_hash_table_iter_init (&eiter, by_expr);
		while (g_hash_table_iter_next (&eiter, &key, &value)) {
			const gchar *expr = key;
			GHashTable *folders = value;
			CamelStoreSearch *search;
			GHashTableIter fiter;

			search = camel_store_search_new (store);
			camel_store_search_set_expression (search, expr);
			g_ptr_array_add (searches, search);

			if (sd->match_indexes) {
				for (ii = 0; ii < sd->match_indexes->len; ii++) {
					CamelStoreSearchIndex *match_index = g_ptr_array_index (sd->match_indexes, ii);

					camel_store_search_add_match_index (search, match_index);
				}
			}

			g_hash_table_iter_init (&fiter, folders);
			while (g_hash_table_iter_next (&fiter, &key, NULL)) {
				CamelFolder *folder = key;

				camel_store_search_add_folder (search, folder);
			}
		}
	}

	/* this extends (copies) the code from the camel-folder.c:folder_search_sync()
	   implementation to work on a set of (sub)folders; making the CamelFolder code
	   generic enough to cover both real and virtual folders would confuse the API */

	for (ii = 0; ii < searches->len && success && !g_cancellable_is_cancelled (cancellable); ii++) {
		CamelStoreSearch *search = g_ptr_array_index (searches, ii);

		success = camel_store_search_rebuild_sync (search, cancellable, error);
		if (!success)
			break;

		if (threads_kind == CAMEL_MATCH_THREADS_KIND_NONE)
			threads_kind = camel_store_search_get_match_threads_kind (search, &threads_flags);
	}

	if (threads_kind != CAMEL_MATCH_THREADS_KIND_NONE) {
		for (ii = 0; ii < searches->len && success && !g_cancellable_is_cancelled (cancellable); ii++) {
			CamelStoreSearch *search = g_ptr_array_index (searches, ii);

			success = camel_store_search_add_match_threads_items_sync (search, &items, cancellable, error);
			if (success) {
				CamelStoreSearchIndex *index;

				index = camel_store_search_ref_result_index (search);
				if (index) {
					if (search_index)
						camel_store_search_index_move_from_existing (search_index, index);
					else
						search_index = camel_store_search_index_ref (index);

					camel_store_search_set_result_index (search, search_index);
				}

				g_clear_pointer (&index, camel_store_search_index_unref);
			}
		}

		if (success && search_index && items)
			camel_store_search_index_apply_match_threads (search_index, items, threads_kind, threads_flags, cancellable);
	}

	for (ii = 0; ii < searches->len && success && !g_cancellable_is_cancelled (cancellable); ii++) {
		CamelStoreSearch *search = g_ptr_array_index (searches, ii);
		GPtrArray *folders;
		guint jj;

		folders = camel_store_search_list_folders (search);
		g_warn_if_fail (folders != NULL);
		if (!folders)
			continue;

		for (jj = 0; success && jj < folders->len && !g_cancellable_is_cancelled (cancellable); jj++) {
			CamelFolder *subfolder = g_ptr_array_index (folders, jj);
			GPtrArray *uids = NULL;
			const gchar *subfolder_id;

			g_warn_if_fail (subfolder != NULL);
			if (!subfolder)
				continue;

			subfolder_id = g_hash_table_lookup (sd->subfolder_ids, subfolder);
			/* it can be missing when it had been processed before, aka included in multiple searches;
			   the camel_store_search_get_uids_sync() consults the result_index, thus it already added
			   all matches for this subfolder */
			if (!subfolder_id)
				continue;

			g_hash_table_remove (current_subfolders, subfolder);

			success = camel_store_search_get_uids_sync (search, camel_folder_get_full_name (subfolder), &uids, cancellable, error);
			if (success) {
				SubfolderData *subfd;

				vee_folder_claim_subfolder_uids (vfolder, subfolder, subfolder_id, uids, changes);
				g_clear_pointer (&uids, g_ptr_array_unref);

				if (!g_hash_table_contains (vfolder->priv->real_subfolders, subfolder_id)) {
					subfd = subfolder_data_new (vfolder, subfolder, subfolder_id);
					g_hash_table_insert (vfolder->priv->real_subfolders, subfd->subfolder_id, subfd);
				}
			}
		}

		g_ptr_array_unref (folders);
	}

	if (!g_cancellable_is_cancelled (cancellable)) {
		g_hash_table_iter_init (&iter, current_subfolders);
		while (g_hash_table_iter_next (&iter, &key, NULL)) {
			CamelFolder *subfolder = key;

			vee_folder_claim_subfolder_uids (vfolder, subfolder, NULL, NULL, changes);
		}
	}

	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);

	g_clear_pointer (&items, g_ptr_array_unref);
	g_clear_pointer (&searches, g_ptr_array_unref);
	g_clear_pointer (&search_index, camel_store_search_index_unref);
	g_clear_pointer (&current_subfolders, g_hash_table_unref);
	search_data_free (sd);

	if (camel_folder_change_info_changed (changes))
		camel_folder_changed (CAMEL_FOLDER (vfolder), changes);
	camel_folder_change_info_free (changes);

	#ifdef ENABLE_MAINTAINER_MODE
	g_signal_emit (vfolder, test_signals[REBUILD_RUN_TEST_SIGNAL], 0, NULL);
	#endif

	return success;
}

static gboolean
vee_folder_rebuild_sync (CamelVeeFolder *vfolder,
			 GCancellable *cancellable,
			 GError **error)
{
	gboolean success;

	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);

	success = vee_folder_rebuild_for_expression_sync (vfolder, vfolder->priv->expression, cancellable, error);

	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);

	return success;
}

/* track vanishing folders */
static void
vee_folder_subfolder_deleted_cb (CamelFolder *subfolder,
				 CamelVeeFolder *self)
{
	camel_vee_folder_remove_folder_sync (self, subfolder, CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD, NULL, NULL);

	if (self->priv->auto_update)
		vee_folder_schedule_rebuild (self, TRUE);
}

static void
vee_folder_dispose (GObject *object)
{
	CamelVeeFolder *vfolder;
	CamelFolder *folder;

	folder = CAMEL_FOLDER (object);
	vfolder = CAMEL_VEE_FOLDER (object);

	/* parent's class frees summary on dispose, thus depend on it */
	if (camel_folder_get_folder_summary (folder)) {
		GPtrArray *subfolders;
		guint ii;

		vfolder->priv->destroyed = TRUE;
		subfolders = camel_vee_folder_dup_folders (vfolder);

		camel_folder_freeze (folder);

		for (ii = 0; ii < subfolders->len; ii++) {
			CamelFolder *subfolder = g_ptr_array_index (subfolders, ii);
			camel_vee_folder_remove_folder_sync (vfolder, subfolder, CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD, NULL, NULL);
		}

		camel_folder_thaw (folder);

		g_ptr_array_unref (subfolders);
	}

	g_rec_mutex_lock (&vfolder->priv->changed_lock);
	if (vfolder->priv->rebuild_cancellable) {
		g_cancellable_cancel (vfolder->priv->rebuild_cancellable);
		g_clear_object (&vfolder->priv->rebuild_cancellable);
	}
	g_rec_mutex_unlock (&vfolder->priv->changed_lock);

	/* Chain up to parent's dispose () method. */
	G_OBJECT_CLASS (camel_vee_folder_parent_class)->dispose (object);
}

static void
vee_folder_finalize (GObject *object)
{
	CamelVeeFolder *vf;

	vf = CAMEL_VEE_FOLDER (object);

	g_free (vf->priv->expression);

	g_clear_pointer (&vf->priv->subfolders, g_hash_table_unref);

	g_rec_mutex_clear (&vf->priv->subfolder_lock);
	g_rec_mutex_clear (&vf->priv->changed_lock);
	g_hash_table_destroy (vf->priv->real_subfolders);

	/* Chain up to parent's finalize () method. */
	G_OBJECT_CLASS (camel_vee_folder_parent_class)->finalize (object);
}

static void
vee_folder_get_property (GObject *object,
                         guint property_id,
                         GValue *value,
                         GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTO_UPDATE:
			g_value_set_boolean (
				value, camel_vee_folder_get_auto_update (
				CAMEL_VEE_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
vee_folder_set_property (GObject *object,
                         guint property_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTO_UPDATE:
			camel_vee_folder_set_auto_update (
				CAMEL_VEE_FOLDER (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static guint
vee_folder_vee_uid_hash (gconstpointer ptr)
{
	const guchar *str = ptr;
	guint ii, res = 0, val = 0;

	if (!str || !*str)
		return 0;

	/* compare only the first 8 letters, which is the real folder's hashed ID */
	for (ii = 0; ii < 8 && str[ii]; ii++) {
		val = (val << 8) | str[ii];
		if (ii == 3 || ii == 7 || str[ii + 1] == 0) {
			res = res ^ val;
			val = 0;
		}
	}

	return val;
}

static gboolean
vee_folder_vee_uid_equal (gconstpointer ptr1,
			  gconstpointer ptr2)
{
	const gchar *str1 = ptr1, *str2 = ptr2;
	guint ii;

	if (!str1 || !*str1 || !str2 || !*str2)
		return g_strcmp0 (str1, str2) == 0;

	for (ii = 0; ii < 8 && str1[ii] && str2[ii]; ii++) {
		if (str1[ii] != str2[ii])
			break;
	}

	return ii == 8;
}

static guint32
vee_folder_get_permanent_flags (CamelFolder *folder)
{
	/* FIXME: what to do about user flags if the subfolder doesn't support them? */
	return CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN;
}

static gboolean
vee_folder_search_sync (CamelFolder *folder,
			const gchar *expression,
			GPtrArray **out_uids,
			GCancellable *cancellable,
			GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (folder), FALSE);
	g_return_val_if_fail (out_uids != NULL, FALSE);

	if (!expression || !*expression || g_strcmp0 (expression, "#t") == 0) {
		*out_uids = camel_folder_dup_uids (folder);
	} else {
		CamelVeeFolder *vfolder;

		vfolder = CAMEL_VEE_FOLDER (camel_vee_folder_new (camel_folder_get_parent_store (folder), "tmp-vee-folder", 0));
		success = camel_vee_folder_add_folder_sync (vfolder, folder, CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD, cancellable, error);
		if (success) {
			success = camel_vee_folder_set_expression_sync (vfolder, expression, CAMEL_VEE_FOLDER_OP_FLAG_NONE, cancellable, error);
			if (success)
				*out_uids = camel_folder_dup_uids (CAMEL_FOLDER (vfolder));
		}

		g_clear_object (&vfolder);
	}

	return success;
}

static void
vee_folder_delete (CamelFolder *folder)
{
	CamelVeeFolder *vfolder;
	GPtrArray *subfolders;
	guint ii;

	g_return_if_fail (CAMEL_IS_VEE_FOLDER (folder));

	vfolder = CAMEL_VEE_FOLDER (folder);

	subfolders = camel_vee_folder_dup_folders (vfolder);

	for (ii = 0; ii < subfolders->len; ii++) {
		CamelFolder *subfolder = g_ptr_array_index (subfolders, ii);

		camel_vee_folder_remove_folder_sync (vfolder, subfolder, CAMEL_VEE_FOLDER_OP_FLAG_NONE, NULL, NULL);
	}

	g_ptr_array_unref (subfolders);

	((CamelFolderClass *) camel_vee_folder_parent_class)->delete_ (folder);
}

static void
vee_folder_freeze (CamelFolder *folder)
{
	CamelVeeFolder *vfolder = CAMEL_VEE_FOLDER (folder);
	GHashTableIter iter;
	gpointer key = NULL;

	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);
	g_hash_table_iter_init (&iter, vfolder->priv->subfolders);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		CamelFolder *subfolder = key;

		camel_folder_freeze (subfolder);
	}
	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);

	/* call parent implementation */
	CAMEL_FOLDER_CLASS (camel_vee_folder_parent_class)->freeze (folder);
}

static void
vee_folder_thaw (CamelFolder *folder)
{
	CamelVeeFolder *vfolder = CAMEL_VEE_FOLDER (folder);
	GHashTableIter iter;
	gpointer key = NULL;

	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);
	g_hash_table_iter_init (&iter, vfolder->priv->subfolders);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		CamelFolder *subfolder = key;

		camel_folder_thaw (subfolder);
	}
	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);

	/* call parent implementation */
	CAMEL_FOLDER_CLASS (camel_vee_folder_parent_class)->thaw (folder);
}

static gboolean
vee_folder_append_message_sync (CamelFolder *folder,
                                CamelMimeMessage *message,
                                CamelMessageInfo *info,
                                gchar **appended_uid,
                                GCancellable *cancellable,
                                GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Cannot copy or move messages into a Virtual Folder"));

	return FALSE;
}

static gboolean
vee_folder_expunge_sync (CamelFolder *folder,
                         GCancellable *cancellable,
                         GError **error)
{
	return CAMEL_FOLDER_GET_CLASS (folder)->
		synchronize_sync (folder, TRUE, cancellable, error);
}

static CamelMimeMessage *
vee_folder_get_message_sync (CamelFolder *folder,
                             const gchar *uid,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelVeeMessageInfo *mi;
	CamelMimeMessage *msg = NULL;

	mi = (CamelVeeMessageInfo *) camel_folder_summary_get (camel_folder_get_folder_summary (folder), uid);
	if (mi) {
		msg = camel_folder_get_message_sync (
			camel_vee_message_info_get_original_folder (mi), camel_message_info_get_uid (CAMEL_MESSAGE_INFO (mi)) + 8,
			cancellable, error);
		g_clear_object (&mi);
	} else {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			/* Translators: The first “%s” is replaced with a message UID, the second “%s”
			   is replaced with an account name and the third “%s” is replaced with a full
			   path name. The spaces around “:” are intentional, as the whole “%s : %s” is
			   meant as an absolute identification of the folder. */
			_("No such message %s in “%s : %s”"), uid,
			camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))),
			camel_folder_get_full_display_name (folder));
	}

	return msg;
}

static CamelMimeMessage *
vee_folder_get_message_cached (CamelFolder *folder,
			       const gchar *message_uid,
			       GCancellable *cancellable)
{
	CamelVeeMessageInfo *mi;
	CamelMimeMessage *msg = NULL;

	mi = (CamelVeeMessageInfo *) camel_folder_summary_get (camel_folder_get_folder_summary (folder), message_uid);
	if (mi) {
		msg = camel_folder_get_message_cached (
			camel_vee_message_info_get_original_folder (mi), camel_message_info_get_uid (CAMEL_MESSAGE_INFO (mi)) + 8,
			cancellable);
		g_clear_object (&mi);
	}

	return msg;
}

static gboolean
vee_folder_refresh_info_sync (CamelFolder *folder,
                              GCancellable *cancellable,
                              GError **error)
{
	return vee_folder_rebuild_sync (CAMEL_VEE_FOLDER (folder), cancellable, error);
}

static gboolean
vee_folder_synchronize_sync (CamelFolder *folder,
                             gboolean expunge,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelVeeFolder *vfolder = (CamelVeeFolder *) folder;
	GHashTableIter iter;
	gpointer key = NULL;
	gboolean res = TRUE;

	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (folder), FALSE);

	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);

	g_hash_table_iter_init (&iter, vfolder->priv->subfolders);
	while (!g_cancellable_is_cancelled (cancellable) && g_hash_table_iter_next (&iter, &key, NULL)) {
		CamelFolder *subfolder = key;
		GError *local_error = NULL;

		if (!expunge && !CAMEL_IS_VEE_FOLDER (subfolder))
			continue;

		if (!camel_folder_synchronize_sync (subfolder, expunge, cancellable, &local_error)) {
			if (local_error && strncmp (local_error->message, "no such table", 13) != 0 && error && !*error) {
				const gchar *desc;

				desc = camel_folder_get_description (subfolder);
				g_propagate_prefixed_error (
					error, local_error,
					_("Error storing “%s”: "), desc);

				res = FALSE;
			} else
				g_clear_error (&local_error);
		}
	}

	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);

	return res;
}

static gboolean
vee_folder_transfer_messages_to_sync (CamelFolder *folder,
                                      GPtrArray *uids,
                                      CamelFolder *dest,
                                      gboolean delete_originals,
                                      GPtrArray **transferred_uids,
                                      GCancellable *cancellable,
                                      GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Cannot copy or move messages into a Virtual Folder"));

	return FALSE;
}

static void
vee_folder_remove_folder (CamelVeeFolder *vfolder,
                          CamelFolder *subfolder,
			  CamelVeeFolderOpFlags op_flags)
{
	CamelFolderChangeInfo *changes;
	CamelFolder *v_folder;
	CamelVeeSummary *vsummary;
	GHashTable *uids;

	if (camel_application_is_exiting || vfolder->priv->destroyed)
		return;

	v_folder = CAMEL_FOLDER (vfolder);
	changes = camel_folder_change_info_new ();

	camel_folder_freeze (v_folder);

	vsummary = CAMEL_VEE_SUMMARY (camel_folder_get_folder_summary (v_folder));
	uids = camel_vee_summary_get_uids_for_subfolder (vsummary, subfolder);
	if (uids) {
		GHashTableIter iter;
		gpointer key = NULL;

		g_hash_table_iter_init (&iter, uids);
		while (g_hash_table_iter_next (&iter, &key, NULL)) {
			const gchar *vuid = key;

			camel_vee_summary_remove (vsummary, subfolder, vuid);
			camel_folder_change_info_remove_uid (changes, vuid);
		}

		g_hash_table_destroy (uids);
	}

	/* do not notify about changes in vfolder which
	 * is removing its subfolders in dispose */
	if (!vfolder->priv->destroyed && !(op_flags & CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD) &&
	    camel_folder_change_info_changed (changes))
		camel_folder_changed (CAMEL_FOLDER (vfolder), changes);
	camel_folder_change_info_free (changes);

	camel_folder_thaw (v_folder);
}

static void
camel_vee_folder_class_init (CamelVeeFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = vee_folder_dispose;
	object_class->finalize = vee_folder_finalize;
	object_class->get_property = vee_folder_get_property;
	object_class->set_property = vee_folder_set_property;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->get_permanent_flags = vee_folder_get_permanent_flags;
	folder_class->search_sync = vee_folder_search_sync;
	folder_class->delete_ = vee_folder_delete;
	folder_class->freeze = vee_folder_freeze;
	folder_class->thaw = vee_folder_thaw;
	folder_class->append_message_sync = vee_folder_append_message_sync;
	folder_class->expunge_sync = vee_folder_expunge_sync;
	folder_class->get_message_sync = vee_folder_get_message_sync;
	folder_class->get_message_cached = vee_folder_get_message_cached;
	folder_class->refresh_info_sync = vee_folder_refresh_info_sync;
	folder_class->synchronize_sync = vee_folder_synchronize_sync;
	folder_class->transfer_messages_to_sync = vee_folder_transfer_messages_to_sync;

	/**
	 * CamelVeeFolder:auto-update
	 *
	 * Automatically update on change in source folders
	 **/
	properties[PROP_AUTO_UPDATE] =
		g_param_spec_boolean (
			"auto-update", NULL,
			_("Automatically _update on change in source folders"),
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			CAMEL_FOLDER_PARAM_PERSISTENT);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/* private signal, to notify other search folders (which contain this one) that its setup like the expression
	   or subfolders changed, thus the listener might need to rebuild its content */
	signals[VEE_SETUP_CHANGED] = g_signal_new (
		"vee-setup-changed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0,
		NULL, NULL, NULL,
		G_TYPE_NONE, 0,
		G_TYPE_NONE);

	#ifdef ENABLE_MAINTAINER_MODE
	test_signals[REBUILD_SCHEDULE_TEST_SIGNAL] = g_signal_new ("rebuild-schedule-test-signal",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0,
		NULL, NULL, NULL,
		G_TYPE_NONE, 0,
		G_TYPE_NONE);

	test_signals[REBUILD_RUN_TEST_SIGNAL] = g_signal_new ("rebuild-run-test-signal",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0,
		NULL, NULL, NULL,
		G_TYPE_NONE, 0,
		G_TYPE_NONE);
	#endif /* ENABLE_MAINTAINER_MODE */

	camel_folder_class_map_legacy_property (folder_class, "auto-update", 0x2401);
}

static void
camel_vee_folder_init (CamelVeeFolder *vee_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (vee_folder);

	vee_folder->priv = camel_vee_folder_get_instance_private (vee_folder);

	camel_folder_set_flags (folder, camel_folder_get_flags (folder) | CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY);

	g_rec_mutex_init (&vee_folder->priv->subfolder_lock);
	g_rec_mutex_init (&vee_folder->priv->changed_lock);

	vee_folder->priv->auto_update = TRUE;
	vee_folder->priv->subfolders = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, NULL);
	vee_folder->priv->real_subfolders = g_hash_table_new_full (vee_folder_vee_uid_hash, vee_folder_vee_uid_equal, NULL, subfolder_data_free);
}

/**
 * camel_vee_folder_construct:
 * @vf: a #CamelVeeFolder
 * @flags: flags for the @vf
 *
 * Initializes internal structures of the @vf. This is meant to be
 * called by the descendants of #CamelVeeFolder.
 **/
void
camel_vee_folder_construct (CamelVeeFolder *vf,
                            guint32 flags)
{
	CamelFolder *folder = (CamelFolder *) vf;
	CamelStore *parent_store;

	vf->priv->flags = flags;

	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (vf));

	camel_folder_take_folder_summary (folder, camel_vee_summary_new (folder));

	/* only for subfolders of vee-store */
	if (CAMEL_IS_VEE_STORE (parent_store)) {
		const gchar *user_data_dir;
		gchar *state_file, *folder_name, *filename;

		user_data_dir = camel_service_get_user_data_dir (CAMEL_SERVICE (parent_store));

		folder_name = g_uri_escape_string (camel_folder_get_full_name (folder), NULL, TRUE);
		filename = g_strconcat (folder_name, ".cmeta", NULL);
		state_file = g_build_filename (user_data_dir, filename, NULL);

		camel_folder_take_state_filename (folder, g_steal_pointer (&state_file));

		g_free (filename);
		g_free (folder_name);

		/* set/load persistent state */
		camel_folder_load_state (folder);
	}
}

/**
 * camel_vee_folder_get_flags:
 * @vf: a #CamelVeeFolder
 *
 * Returns: flags of @vf, as set by camel_vee_folder_construct()
 *
 * Since: 3.24
 **/
guint32
camel_vee_folder_get_flags (CamelVeeFolder *vf)
{
	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vf), 0);

	return vf->priv->flags;
}

/**
 * camel_vee_folder_new:
 * @parent_store: the parent CamelVeeStore
 * @full: the full path to the vfolder.
 * @flags: flags of some kind
 *
 * Returns: (transfer full): A new @CamelVeeFolder object. Unref it
 *    with g_object_unref() when no longer needed.
 **/
CamelFolder *
camel_vee_folder_new (CamelStore *parent_store,
                      const gchar *full,
                      guint32 flags)
{
	CamelVeeFolder *vf;
	const gchar *name;

	g_return_val_if_fail (CAMEL_IS_STORE (parent_store), NULL);
	g_return_val_if_fail (full != NULL, NULL);

	name = strrchr (full, '/');

	if (name == NULL)
		name = full;
	else
		name++;

	vf = g_object_new (CAMEL_TYPE_VEE_FOLDER,
		"display-name", name,
		"full-name", full,
		"parent-store", parent_store,
		NULL);
	camel_vee_folder_construct (vf, flags);

	d (printf ("returning folder %s %p, count = %d\n", full, vf, camel_folder_get_message_count ((CamelFolder *) vf)));

	return (CamelFolder *) vf;
}

/**
 * camel_vee_folder_set_expression_sync:
 * @vfolder: a #CamelVeeFolder
 * @expression: an SExp expression to set
 * @op_flags: bit-or of #CamelVeeFolderOpFlags
 * @cancellable: a #GCancellable, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Sets an SExp expression to be used for this @vfolder
 * and updates its content. The expression is not changed
 * when the call fails.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_vee_folder_set_expression_sync (CamelVeeFolder *vfolder,
				      const gchar *expression,
				      CamelVeeFolderOpFlags op_flags,
				      GCancellable *cancellable,
				      GError **error)
{
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vfolder), FALSE);

	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);

	/* no change, do nothing */
	if ((vfolder->priv->expression && expression && g_strcmp0 (vfolder->priv->expression, expression) == 0)
	    || (vfolder->priv->expression == NULL && expression == NULL)) {
		g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);
		return TRUE;
	}

	success = (op_flags & CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD) != 0 ||
		vee_folder_rebuild_for_expression_sync (vfolder, expression, cancellable, error);

	if (success) {
		g_free (vfolder->priv->expression);
		vfolder->priv->expression = g_strdup (expression);
	}

	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);

	if (success && !(op_flags & CAMEL_VEE_FOLDER_OP_FLAG_SKIP_EMIT))
		vee_folder_emit_setup_changed (vfolder);

	return success;
}

/**
 * camel_vee_folder_get_expression:
 * @vfolder: a #CamelVeeFolder
 *
 * Returns: (transfer none): a SExp expression used for this @vfolder
 *
 * Since: 3.6
 **/
const gchar *
camel_vee_folder_get_expression (CamelVeeFolder *vfolder)
{
	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vfolder), NULL);

	return vfolder->priv->expression;
}

/**
 * camel_vee_folder_add_folder_sync:
 * @vfolder: a #CamelVeeFolder
 * @subfolder: source CamelFolder to add to @vfolder
 * @op_flags: bit-or of #CamelVeeFolderOpFlags
 * @cancellable: a #GCancellable, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Adds @subfolder as a source folder to @vfolder.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_vee_folder_add_folder_sync (CamelVeeFolder *vfolder,
				  CamelFolder *subfolder,
				  CamelVeeFolderOpFlags op_flags,
				  GCancellable *cancellable,
				  GError **error)
{
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vfolder), FALSE);
	g_return_val_if_fail (CAMEL_IS_FOLDER (subfolder), FALSE);

	if (vfolder == (CamelVeeFolder *) subfolder) {
		g_warning ("Adding a virtual folder to itself as source, ignored");
		return TRUE;
	}

	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);

	if (!g_hash_table_contains (vfolder->priv->subfolders, subfolder)) {
		gint freeze_count;

		g_hash_table_add (vfolder->priv->subfolders, g_object_ref (subfolder));

		freeze_count = camel_folder_get_frozen_count (CAMEL_FOLDER (vfolder));
		while (freeze_count > 0) {
			camel_folder_freeze (subfolder);
			freeze_count--;
		}
	} else {
		/* nothing to do, it's already there */
		g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);
		return TRUE;
	}

	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);

	g_signal_connect (
		subfolder, "deleted",
		G_CALLBACK (vee_folder_subfolder_deleted_cb), vfolder);

	if (CAMEL_IS_VEE_FOLDER (subfolder)) {
		g_signal_connect_object (subfolder, "vee-setup-changed",
			G_CALLBACK (vee_folder_subfolder_vee_setup_changed_cb), vfolder, 0);
	}

	success = (op_flags & CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD) != 0 ||
		vee_folder_rebuild_sync (vfolder, cancellable, error);

	if (success && !(op_flags & CAMEL_VEE_FOLDER_OP_FLAG_SKIP_EMIT))
		vee_folder_emit_setup_changed (vfolder);

	return success;
}

/**
 * camel_vee_folder_remove_folder_sync:
 * @vfolder: a #CamelVeeFolder
 * @subfolder: source CamelFolder to remove from @vfolder
 * @op_flags: bit-or of #CamelVeeFolderOpFlags
 * @cancellable: a #GCancellable, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Removed the source folder, @subfolder, from the virtual folder, @vfolder.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_vee_folder_remove_folder_sync (CamelVeeFolder *vfolder,
				     CamelFolder *subfolder,
				     CamelVeeFolderOpFlags op_flags,
				     GCancellable *cancellable,
				     GError **error)
{
	gboolean success;
	gint freeze_count;

	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vfolder), FALSE);
	g_return_val_if_fail (CAMEL_IS_FOLDER (subfolder), FALSE);

	g_object_ref (subfolder);

	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);

	if (!g_hash_table_remove (vfolder->priv->subfolders, subfolder)) {
		g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);
		g_object_unref (subfolder);
		return TRUE;
	}

	g_signal_handlers_disconnect_by_func (subfolder, vee_folder_subfolder_deleted_cb, vfolder);
	g_signal_handlers_disconnect_by_func (subfolder, vee_folder_subfolder_vee_setup_changed_cb, vfolder);

	freeze_count = camel_folder_get_frozen_count (CAMEL_FOLDER (vfolder));
	while (freeze_count > 0) {
		camel_folder_thaw (subfolder);
		freeze_count--;
	}

	vee_folder_remove_folder (vfolder, subfolder, op_flags);

	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);
	g_object_unref (subfolder);

	success = (op_flags & CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD) != 0 ||
		vee_folder_rebuild_sync (vfolder, cancellable, error);

	if (success && !(op_flags & CAMEL_VEE_FOLDER_OP_FLAG_SKIP_EMIT))
		vee_folder_emit_setup_changed (vfolder);

	return success;
}

static void
remove_folders (CamelFolder *folder,
                CamelFolder *foldercopy,
                CamelVeeFolder *vf)
{
	camel_vee_folder_remove_folder_sync (vf, folder, CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD | CAMEL_VEE_FOLDER_OP_FLAG_SKIP_EMIT, NULL, NULL);
	g_object_unref (folder);
}

/**
 * camel_vee_folder_set_folders_sync:
 * @vfolder: a #CamelVeeFolder
 * @folders: (element-type CamelFolder) (transfer none): a #GPtrArray of #CamelFolder to add
 * @op_flags: bit-or of #CamelVeeFolderOpFlags
 * @cancellable: a #GCancellable, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Set the whole list of folder sources on a search folder.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_vee_folder_set_folders_sync (CamelVeeFolder *vfolder,
				   GPtrArray *folders,
				   CamelVeeFolderOpFlags op_flags,
				   GCancellable *cancellable,
				   GError **error)
{
	GHashTable *remove;
	GPtrArray *to_add;
	CamelFolder *subfolder;
	GHashTableIter iter;
	gpointer key = NULL;
	guint ii;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vfolder), FALSE);

	remove = g_hash_table_new (NULL, NULL);

	/* setup a table of all folders we have currently */
	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);
	g_hash_table_iter_init (&iter, vfolder->priv->subfolders);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		subfolder = key;
		g_hash_table_add (remove, g_object_ref (subfolder));
	}

	camel_folder_freeze (CAMEL_FOLDER (vfolder));

	to_add = g_ptr_array_new_full (folders->len, g_object_unref);

	/* if we already have the folder, ignore it, otherwise mark to add it */
	for (ii = 0; ii < folders->len; ii++) {
		subfolder = g_ptr_array_index (folders, ii);
		if (g_hash_table_remove (remove, subfolder)) {
			g_object_unref (subfolder);
		} else {
			g_ptr_array_add (to_add, g_object_ref (subfolder));
		}
	}

	/* first remove any we still have */
	g_hash_table_foreach (remove, (GHFunc) remove_folders, vfolder);
	g_hash_table_destroy (remove);

	/* then add those new */
	for (ii = 0; ii < to_add->len; ii++) {
		subfolder = g_ptr_array_index (to_add, ii);

		camel_vee_folder_add_folder_sync (vfolder, subfolder, CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD | CAMEL_VEE_FOLDER_OP_FLAG_SKIP_EMIT, NULL, NULL);
	}

	g_ptr_array_unref (to_add);

	camel_folder_thaw (CAMEL_FOLDER (vfolder));

	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);

	success = (op_flags & CAMEL_VEE_FOLDER_OP_FLAG_SKIP_REBUILD) != 0 ||
		vee_folder_rebuild_sync (vfolder, cancellable, error);

	if (success && !(op_flags & CAMEL_VEE_FOLDER_OP_FLAG_SKIP_EMIT))
		vee_folder_emit_setup_changed (vfolder);

	return success;
}

/**
 * camel_vee_folder_dup_folders:
 * @vfolder: a #CamelVeeFolder
 *
 * Returns a #GPtrArray of all folders of this @vfolder, which
 * are used to populate it. These are in no particular order.
 *
 * Free the returned array with g_ptr_array_unref(), when no longer needed.
 *
 * Returns: (transfer container) (element-type CamelFolder): a #GPtrArray
 *    of all the folders of this @vfolder.
 *
 * Since: 3.58
 **/
GPtrArray *
camel_vee_folder_dup_folders (CamelVeeFolder *vfolder)
{
	GPtrArray *folders;
	GHashTableIter iter;
	gpointer key;

	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vfolder), NULL);

	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);

	folders = g_ptr_array_new_full (g_hash_table_size (vfolder->priv->subfolders), g_object_unref);

	g_hash_table_iter_init (&iter, vfolder->priv->subfolders);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		CamelFolder *subfolder = key;
		g_ptr_array_add (folders, g_object_ref (subfolder));
	}
	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);

	return folders;
}

/**
 * camel_vee_folder_get_location:
 * @vf: a #CamelVeeFolder
 * @vinfo: a #CamelVeeMessageInfo to search for
 * @realuid: (out) (transfer full) (nullable): if not %NULL, set to the UID of the real message info
 *
 * Find the real folder (and message info UID) for the given @vinfo.
 * When the @realuid is not %NULL and it's set, then use g_free() to
 * free it, when no longer needed.
 *
 * Returns: (transfer none): a real (not virtual) #CamelFolder, which the @vinfo is for.
 **/
CamelFolder *
camel_vee_folder_get_location (CamelVeeFolder *vf,
                               const CamelVeeMessageInfo *vinfo,
                               gchar **realuid)
{
	CamelFolder *folder;
	const gchar *uid;

	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vf), NULL);
	g_return_val_if_fail (vinfo != NULL, NULL);

	folder = camel_vee_message_info_get_original_folder (vinfo);
	uid = camel_message_info_get_uid (CAMEL_MESSAGE_INFO (vinfo));

	g_return_val_if_fail (uid != NULL && strlen (uid) > 8, NULL);

	/* locking?  yes?  no?  although the vfolderinfo is valid when obtained
	 * the folder in it might not necessarily be so ...? */
	if (CAMEL_IS_VEE_FOLDER (folder)) {
		CamelFolder *res;
		CamelMessageInfo *vfinfo;

		vfinfo = camel_folder_get_message_info (folder, uid + 8);
		res = camel_vee_folder_get_location ((CamelVeeFolder *) folder, CAMEL_VEE_MESSAGE_INFO (vfinfo), realuid);
		g_clear_object (&vfinfo);
		return res;
	} else {
		if (realuid)
			*realuid = g_strdup (uid + 8);

		return folder;
	}
}

/**
 * camel_vee_folder_dup_vee_uid_folder:
 * @vfolder: a #CamelVeeFolder
 * @vee_message_uid: a virtual message info UID
 *
 * Returns: (transfer full) (nullable): a #CamelFolder to which the @vee_message_uid
 *    belongs, or %NULL, when it could not be found.
 *
 * Since: 3.6
 **/
CamelFolder *
camel_vee_folder_dup_vee_uid_folder (CamelVeeFolder *vfolder,
                                     const gchar *vee_message_uid)
{
	CamelFolder *res = NULL;
	SubfolderData *sd;

	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vfolder), NULL);
	g_return_val_if_fail (vee_message_uid, NULL);

	g_rec_mutex_lock (&vfolder->priv->subfolder_lock);

	sd = g_hash_table_lookup (vfolder->priv->real_subfolders, vee_message_uid);
	if (sd)
		res = g_object_ref (sd->folder);

	g_rec_mutex_unlock (&vfolder->priv->subfolder_lock);

	return res;
}

/**
 * camel_vee_folder_set_auto_update:
 * @vfolder: a #CamelVeeFolder
 * @auto_update: a value to set
 *
 * Sets whether the @vfolder can automatically update when of its
 * subfolders changes.
 *
 * Since: 3.6
 **/
void
camel_vee_folder_set_auto_update (CamelVeeFolder *vfolder,
                                  gboolean auto_update)
{
	g_return_if_fail (CAMEL_IS_VEE_FOLDER (vfolder));

	if (vfolder->priv->auto_update == auto_update)
		return;

	vfolder->priv->auto_update = auto_update;

	g_object_notify_by_pspec (G_OBJECT (vfolder), properties[PROP_AUTO_UPDATE]);
}

/**
 * camel_vee_folder_get_auto_update:
 * @vfolder: a #CamelVeeFolder
 *
 * Returns: whether the @vfolder can automatically update when any
 *    of its subfolders changes.
 *
 * Since: 3.6
 **/
gboolean
camel_vee_folder_get_auto_update (CamelVeeFolder *vfolder)
{
	g_return_val_if_fail (CAMEL_IS_VEE_FOLDER (vfolder), FALSE);

	return vfolder->priv->auto_update;
}

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
 * Authors: Jeffrey Stedfast <fejj@novell.com>
 */

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "camel-debug.h"
#include "camel-enumtypes.h"
#include "camel-offline-folder.h"
#include "camel-offline-settings.h"
#include "camel-offline-store.h"
#include "camel-operation.h"
#include "camel-session.h"
#include "camel-string-utils.h"
#include "camel-utils.h"

typedef struct _AsyncContext AsyncContext;
typedef struct _OfflineDownsyncData OfflineDownsyncData;

struct _CamelOfflineFolderPrivate {
	CamelThreeState offline_sync;

	GMutex ongoing_downloads_lock;
	GHashTable *ongoing_downloads; /* gchar * UID ~> g_thread_self() */
};

struct _AsyncContext {
	gchar *expression;
};

struct _OfflineDownsyncData {
	CamelFolder *folder;
	CamelFolderChangeInfo *changes;
};

enum {
	PROP_0,
	PROP_OFFLINE_SYNC,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (CamelOfflineFolder, camel_offline_folder, CAMEL_TYPE_FOLDER)

static void
async_context_free (AsyncContext *async_context)
{
	g_free (async_context->expression);

	g_slice_free (AsyncContext, async_context);
}

static gboolean
offline_folder_synchronize_message_wrapper_sync (CamelFolder *folder,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error)
{
	CamelOfflineFolder *offline_folder;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_OFFLINE_FOLDER (folder), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	offline_folder = CAMEL_OFFLINE_FOLDER (folder);

	g_mutex_lock (&offline_folder->priv->ongoing_downloads_lock);
	/* Another thread is downloading this message already, do not wait for it,
	   just move on to the next one. In case the previous/ongoing download
	   fails the message won't be downloaded. */
	if (g_hash_table_contains (offline_folder->priv->ongoing_downloads, uid)) {
		if (camel_debug ("downsync")) {
			printf ("[downsync]          %p: (%s : %s): skipping uid '%s', because it is already downloading by thread %p (this thread is %p)\n", camel_folder_get_parent_store (folder),
				camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))), camel_folder_get_full_name (folder),
				uid, g_hash_table_lookup (offline_folder->priv->ongoing_downloads, uid), g_thread_self ());
		}
		g_mutex_unlock (&offline_folder->priv->ongoing_downloads_lock);
		return TRUE;
	} else {
		g_hash_table_insert (offline_folder->priv->ongoing_downloads, (gpointer) camel_pstring_strdup (uid), g_thread_self ());
	}

	if (camel_debug ("downsync")) {
		printf ("[downsync]          %p: (%s : %s): going to download uid '%s' in thread %p\n", camel_folder_get_parent_store (folder),
			camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))), camel_folder_get_full_name (folder),
			uid, g_thread_self ());
	}

	g_mutex_unlock (&offline_folder->priv->ongoing_downloads_lock);

	success = camel_folder_synchronize_message_sync (folder, uid, cancellable, error);

	g_mutex_lock (&offline_folder->priv->ongoing_downloads_lock);
	g_warn_if_fail (g_hash_table_remove (offline_folder->priv->ongoing_downloads, uid));

	if (camel_debug ("downsync")) {
		printf ("[downsync]          %p: (%s : %s): finished download of uid '%s' in thread %p, %s\n", camel_folder_get_parent_store (folder),
			camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))), camel_folder_get_full_name (folder),
			uid, g_thread_self (), success ? "success" : "failure");
	}

	g_mutex_unlock (&offline_folder->priv->ongoing_downloads_lock);

	return success;
}

static void
offline_downsync_data_free (OfflineDownsyncData *data)
{
	if (data->changes != NULL)
		camel_folder_change_info_free (data->changes);

	g_object_unref (data->folder);

	g_slice_free (OfflineDownsyncData, data);
}

/* Returns 0, if not set any limit */
static gint64
offline_folder_get_limit_time (CamelFolder *folder)
{
	CamelStore *parent_store;
	CamelSettings *settings;
	gint64 limit_time = 0;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	parent_store = camel_folder_get_parent_store (folder);
	if (!parent_store)
		return limit_time;

	settings = camel_service_ref_settings (CAMEL_SERVICE (parent_store));

	if (CAMEL_IS_OFFLINE_SETTINGS (settings) &&
	    camel_offline_settings_get_limit_by_age (CAMEL_OFFLINE_SETTINGS (settings))) {
		limit_time = camel_time_value_apply (-1,
			camel_offline_settings_get_limit_unit (CAMEL_OFFLINE_SETTINGS (settings)),
			-camel_offline_settings_get_limit_value (CAMEL_OFFLINE_SETTINGS (settings)));
	}

	g_clear_object (&settings);

	return limit_time;
}

static void
offline_folder_downsync_background (CamelSession *session,
                                    GCancellable *cancellable,
                                    OfflineDownsyncData *data,
                                    GError **error)
{
	gint64 limit_time;

	camel_operation_push_message (
		/* Translators: The first “%s” is replaced with an account name and the second “%s”
		   is replaced with a full path name. The spaces around “:” are intentional, as
		   the whole “%s : %s” is meant as an absolute identification of the folder. */
		cancellable, _("Downloading new messages for offline mode in “%s : %s”"),
		camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (data->folder))),
		camel_folder_get_full_display_name (data->folder));

	limit_time = offline_folder_get_limit_time (data->folder);

	if (data->changes) {
		GPtrArray *uid_added;
		gboolean success = TRUE;
		gint ii;

		uid_added = data->changes->uid_added;

		for (ii = 0; success && ii < uid_added->len; ii++) {
			GError *local_error = NULL;
			const gchar *uid;
			gint percent;

			percent = ii * 100 / uid_added->len;
			uid = g_ptr_array_index (uid_added, ii);

			camel_operation_progress (cancellable, percent);

			if (limit_time > 0) {
				CamelMessageInfo *mi;
				gboolean download;

				mi = camel_folder_get_message_info (data->folder, uid);
				if (!mi)
					continue;

				download = camel_message_info_get_date_sent (mi) > limit_time;

				g_clear_object (&mi);

				if (!download)
					continue;
			}

			success = offline_folder_synchronize_message_wrapper_sync (data->folder, uid, cancellable, &local_error);

			if (!success) {
				if (g_error_matches (local_error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_UID)) {
					/* The message can be moved away before the offline download gets to it */
					success = TRUE;
				}

				if (camel_debug ("downsync")) {
					printf ("[downsync]          %p: (%s : %s): failed to download uid '%s' with error '%s', will %s\n",
						camel_folder_get_parent_store (data->folder),
						camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (data->folder))),
						camel_folder_get_full_name (data->folder),
						uid,
						local_error ? local_error->message : "Unknown error",
						success ? "continue" : "stop");
				}
			}

			/* Do not bother the user with background for-offline download errors */
			g_clear_error (&local_error);
		}
	} else {
		gchar *expression = NULL;
		gboolean success;
		GError *local_error = NULL;

		if (limit_time > 0)
			expression = g_strdup_printf ("(match-all (> (get-sent-date) %" G_GINT64_FORMAT ")", limit_time);

		success = camel_offline_folder_downsync_sync (
			CAMEL_OFFLINE_FOLDER (data->folder),
			expression ? expression : "(match-all)", cancellable, &local_error);

		if (camel_debug ("downsync")) {
			printf ("[downsync]          %p: (%s : %s): download of UID-s with%s time limit %s%s\n",
				camel_folder_get_parent_store (data->folder),
				camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (data->folder))),
				camel_folder_get_full_name (data->folder),
				expression ? "" : "out",
				success ? "succeeded" : "failed: ",
				success ? "" : (local_error ? local_error->message : "Unknown error"));
		}

		/* Do not bother the user with background for-offline download errors */
		g_clear_error (&local_error);
		g_free (expression);
	}

	camel_operation_pop_message (cancellable);
}

static void
offline_folder_changed (CamelFolder *folder,
                        CamelFolderChangeInfo *changes)
{
	CamelStore *store;
	CamelSession *session;

	store = camel_folder_get_parent_store (folder);
	if (!store)
		return;

	session = camel_service_ref_session (CAMEL_SERVICE (store));

	if (!session)
		return;

	if (changes && changes->uid_added->len > 0 && camel_offline_folder_can_downsync (CAMEL_OFFLINE_FOLDER (folder))) {
		OfflineDownsyncData *data;
		gchar *description;

		data = g_slice_new0 (OfflineDownsyncData);
		data->changes = camel_folder_change_info_new ();
		camel_folder_change_info_cat (data->changes, changes);
		data->folder = g_object_ref (folder);

		/* Translators: The first “%s” is replaced with an account name and the second “%s”
		   is replaced with a full path name. The spaces around “:” are intentional, as
		   the whole “%s : %s” is meant as an absolute identification of the folder. */
		description = g_strdup_printf (_("Checking download of new messages for offline in “%s : %s”"),
			camel_service_get_display_name (CAMEL_SERVICE (store)),
			camel_folder_get_full_display_name (folder));

		camel_session_submit_job (
			session, description, (CamelSessionCallback)
			offline_folder_downsync_background, data,
			(GDestroyNotify) offline_downsync_data_free);

		g_free (description);
	}

	g_object_unref (session);
}

static void
offline_folder_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_OFFLINE_SYNC:
			camel_offline_folder_set_offline_sync (
				CAMEL_OFFLINE_FOLDER (object),
				g_value_get_enum (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
offline_folder_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_OFFLINE_SYNC:
			g_value_set_enum (
				value, camel_offline_folder_get_offline_sync (
				CAMEL_OFFLINE_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
offline_folder_finalize (GObject *object)
{
	CamelOfflineFolder *offline_folder = CAMEL_OFFLINE_FOLDER (object);

	g_mutex_clear (&offline_folder->priv->ongoing_downloads_lock);

	g_hash_table_destroy (offline_folder->priv->ongoing_downloads);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_offline_folder_parent_class)->finalize (object);
}

static gboolean
offline_folder_downsync_sync (CamelOfflineFolder *offline,
                              const gchar *expression,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelFolder *folder = (CamelFolder *) offline;
	GPtrArray *uids, *uncached_uids = NULL;
	gint64 limit_time;
	gint i;

	/* Translators: The first “%s” is replaced with an account name and the second “%s”
	   is replaced with a full path name. The spaces around “:” are intentional, as
	   the whole “%s : %s” is meant as an absolute identification of the folder. */
	camel_operation_push_message (cancellable, _("Syncing messages in folder “%s : %s” to disk"),
		camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))),
		camel_folder_get_full_display_name (folder));

	limit_time = offline_folder_get_limit_time (folder);
	if (limit_time > 0 && camel_folder_get_folder_summary (folder)) {
		GError *local_error = NULL;
		gchar *search_sexp;

		camel_folder_summary_prepare_fetch_all (camel_folder_get_folder_summary (folder), NULL);

		/* Also used to know what function to use to free the 'uids' array below */
		if (!expression)
			expression = "";

		search_sexp = g_strdup_printf ("(match-all (and "
			"%s "
			"(> (get-sent-date) %" G_GINT64_FORMAT ")"
			"))", expression, (gint64) limit_time);

		if (!camel_folder_search_sync (folder, search_sexp, &uids, cancellable, &local_error)) {
			g_warning ("%s: Failed to search folder '%s': %s", G_STRFUNC, camel_folder_get_full_name (folder),
				local_error ? local_error->message : "Unknown error");
		}

		g_clear_error (&local_error);

		if (camel_debug ("downsync")) {
			printf ("[downsync]       %p: (%s : %s): got %d uids for limit expr '%s'\n", camel_folder_get_parent_store (folder),
				camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))), camel_folder_get_full_name (folder),
				uids ? uids->len : -1, search_sexp);
		}

		g_free (search_sexp);
	} else if (expression) {
		GError *local_error = NULL;

		if (!camel_folder_search_sync (folder, expression, &uids, cancellable, &local_error)) {
			g_warning ("%s: Failed to search folder '%s': %s", G_STRFUNC, camel_folder_get_full_name (folder),
				local_error ? local_error->message : "Unknown error");
		}

		g_clear_error (&local_error);

		if (camel_debug ("downsync")) {
			printf ("[downsync]       %p: (%s : %s): got %d uids for expr '%s'\n", camel_folder_get_parent_store (folder),
				camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))), camel_folder_get_full_name (folder),
				uids ? uids->len : -1, expression);
		}
	} else {
		uids = camel_folder_dup_uids (folder);

		if (camel_debug ("downsync")) {
			printf ("[downsync]       %p: (%s : %s): got all %d uids\n", camel_folder_get_parent_store (folder),
				camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))), camel_folder_get_full_name (folder),
				uids ? uids->len : -1);
		}
	}

	if (!uids)
		goto done;

	uncached_uids = camel_folder_dup_uncached_uids (folder, uids, NULL);

	g_clear_pointer (&uids, g_ptr_array_unref);

	if (camel_debug ("downsync")) {
		printf ("[downsync]       %p: (%s : %s): claimed %d uncached uids\n", camel_folder_get_parent_store (folder),
			camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))), camel_folder_get_full_name (folder),
			uncached_uids ? uncached_uids->len : -1);
	}

	if (!uncached_uids)
		goto done;

	for (i = 0; i < uncached_uids->len && !g_cancellable_is_cancelled (cancellable); i++) {
		const gchar *uid = uncached_uids->pdata[i];
		gboolean download = limit_time <= 0;

		if (limit_time > 0) {
			CamelMessageInfo *mi;

			mi = camel_folder_get_message_info (folder, uid);
			if (!mi) {
				if (camel_debug ("downsync")) {
					printf ("[downsync]          %p: (%s : %s): mi for uid '%s' not found, skipping\n", camel_folder_get_parent_store (folder),
						camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))), camel_folder_get_full_name (folder),
						uid);
				}
				continue;
			}

			download = camel_message_info_get_date_sent (mi) > limit_time;

			g_clear_object (&mi);
		}

		if (download) {
			GError *local_error = NULL;
			/* Translators: The first “%d” is the sequence number of the message, the second “%d”
			   is the total number of messages to synchronize.
			   The first “%s” is replaced with an account name and the second “%s”
			   is replaced with a full path name. The spaces around “:” are intentional, as
			   the whole “%s : %s” is meant as an absolute identification of the folder. */
			camel_operation_push_message (cancellable, _("Syncing message %d of %d in folder “%s : %s” to disk"),
				i + 1, uncached_uids->len,
				camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))),
				camel_folder_get_full_display_name (folder));

			/* Maybe stop on failure */
			if (!offline_folder_synchronize_message_wrapper_sync (folder, uid, cancellable, &local_error)) {
				gboolean can_continue;

				/* The message can be moved away before the offline download gets to it */
				can_continue = g_error_matches (local_error, CAMEL_FOLDER_ERROR, CAMEL_FOLDER_ERROR_INVALID_UID);

				if (camel_debug ("downsync")) {
					printf ("[downsync]          %p: (%s : %s): failed to download uid:%s error:%s; will %s\n", camel_folder_get_parent_store (folder),
						camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))), camel_folder_get_full_name (folder),
						uid, local_error ? local_error->message : "Unknown error", can_continue ? "continue" : "stop");
				}
				g_clear_error (&local_error);
				camel_operation_pop_message (cancellable);

				if (can_continue)
					continue;
				else
					break;
			}

			camel_operation_pop_message (cancellable);
		} else if (camel_debug ("downsync")) {
			printf ("[downsync]       %p: (%s : %s): skipping download of uid '%s'\n", camel_folder_get_parent_store (folder),
				camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))), camel_folder_get_full_name (folder),
				uid);
		}

		camel_operation_progress (cancellable, i * 100 / uncached_uids->len);
	}

 done:
	if (uncached_uids)
		g_clear_pointer (&uncached_uids, g_ptr_array_unref);

	camel_operation_pop_message (cancellable);

	return TRUE;
}

static void
camel_offline_folder_class_init (CamelOfflineFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = offline_folder_set_property;
	object_class->get_property = offline_folder_get_property;
	object_class->finalize = offline_folder_finalize;

	class->downsync_sync = offline_folder_downsync_sync;

	/**
	 * CamelOfflineFolder:offline-sync
	 *
	 * Copy folder content locally for offline operation
	 **/
	properties[PROP_OFFLINE_SYNC] =
		g_param_spec_enum (
			"offline-sync", NULL,
			_("Copy folder content locally for _offline operation"),
			CAMEL_TYPE_THREE_STATE,
			CAMEL_THREE_STATE_INCONSISTENT,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			CAMEL_FOLDER_PARAM_PERSISTENT);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	folder_class = CAMEL_FOLDER_CLASS (class);
	camel_folder_class_map_legacy_property (folder_class, "offline-sync", 0x2400);
}

static void
camel_offline_folder_init (CamelOfflineFolder *folder)
{
	folder->priv = camel_offline_folder_get_instance_private (folder);

	g_mutex_init (&folder->priv->ongoing_downloads_lock);
	folder->priv->offline_sync = CAMEL_THREE_STATE_INCONSISTENT;
	folder->priv->ongoing_downloads = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) camel_pstring_free, NULL);

	g_signal_connect (
		folder, "changed",
		G_CALLBACK (offline_folder_changed), NULL);
}

/**
 * camel_offline_folder_get_offline_sync:
 * @folder: a #CamelOfflineFolder
 *
 * Since: 2.32
 **/
CamelThreeState
camel_offline_folder_get_offline_sync (CamelOfflineFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_OFFLINE_FOLDER (folder), CAMEL_THREE_STATE_INCONSISTENT);

	return folder->priv->offline_sync;
}

/**
 * camel_offline_folder_set_offline_sync:
 * @folder: a #CamelOfflineFolder
 * @offline_sync: whether to synchronize for offline use, as a #CamelThreeState enum
 *
 * The %CAMEL_THREE_STATE_INCONSISTENT means what the parent store has set.
 *
 * Since: 2.32
 **/
void
camel_offline_folder_set_offline_sync (CamelOfflineFolder *folder,
                                       CamelThreeState offline_sync)
{
	g_return_if_fail (CAMEL_IS_OFFLINE_FOLDER (folder));

	if (folder->priv->offline_sync == offline_sync)
		return;

	folder->priv->offline_sync = offline_sync;

	g_object_notify_by_pspec (G_OBJECT (folder), properties[PROP_OFFLINE_SYNC]);
}

/**
 * camel_offline_folder_can_downsync:
 * @folder: a #CamelOfflineFolder
 *
 * Checks whether the @folder can run downsync according to its
 * settings (camel_offline_folder_get_offline_sync()) and to
 * the parent's #CamelOfflineStore settings (camel_offline_settings_get_stay_synchronized()).
 *
 * Returns: %TRUE, when the @folder can be synchronized for offline; %FALSE otherwise.
 *
 * Since: 3.22
 **/
gboolean
camel_offline_folder_can_downsync (CamelOfflineFolder *folder)
{
	CamelService *service;
	CamelSettings *settings;
	CamelStore *parent_store;
	CamelThreeState sync_folder;
	gboolean sync_store;

	g_return_val_if_fail (CAMEL_IS_OFFLINE_FOLDER (folder), FALSE);

	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (folder));
	if (!parent_store)
		return FALSE;

	service = CAMEL_SERVICE (parent_store);
	settings = camel_service_ref_settings (service);

	sync_store = camel_offline_settings_get_stay_synchronized (CAMEL_OFFLINE_SETTINGS (settings));

	g_object_unref (settings);

	sync_folder = camel_offline_folder_get_offline_sync (CAMEL_OFFLINE_FOLDER (folder));

	return sync_folder == CAMEL_THREE_STATE_ON || (sync_store && sync_folder == CAMEL_THREE_STATE_INCONSISTENT);
}

/**
 * camel_offline_folder_downsync_sync:
 * @folder: a #CamelOfflineFolder
 * @expression: (nullable): search expression describing which set of messages
 *              to downsync (%NULL for all)
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Synchronizes messages in @folder described by the search @expression to
 * the local machine for offline availability.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_offline_folder_downsync_sync (CamelOfflineFolder *folder,
                                    const gchar *expression,
                                    GCancellable *cancellable,
                                    GError **error)
{
	CamelOfflineFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_OFFLINE_FOLDER (folder), FALSE);

	class = CAMEL_OFFLINE_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->downsync_sync != NULL, FALSE);

	success = class->downsync_sync (
		folder, expression, cancellable, error);
	CAMEL_CHECK_GERROR (folder, downsync_sync, success, error);

	return success;
}

/* Helper for camel_offline_folder_downsync() */
static void
offline_folder_downsync_thread (GTask *task,
                                gpointer source_object,
                                gpointer task_data,
                                GCancellable *cancellable)
{
	gboolean success;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	success = camel_offline_folder_downsync_sync (
		CAMEL_OFFLINE_FOLDER (source_object),
		async_context->expression,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * camel_offline_folder_downsync:
 * @folder: a #CamelOfflineFolder
 * @expression: (nullable): search expression describing which set of messages
 *              to downsync (%NULL for all)
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Synchronizes messages in @folder described by the search @expression to
 * the local machine asynchronously for offline availability.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_offline_folder_downsync_finish() to get the result of the
 * operation.
 *
 * Since: 3.0
 **/
void
camel_offline_folder_downsync (CamelOfflineFolder *folder,
                               const gchar *expression,
                               gint io_priority,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_OFFLINE_FOLDER (folder));

	async_context = g_slice_new0 (AsyncContext);
	async_context->expression = g_strdup (expression);

	task = g_task_new (folder, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_offline_folder_downsync);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, offline_folder_downsync_thread);

	g_object_unref (task);
}

/**
 * camel_offline_folder_downsync_finish:
 * @folder: a #CamelOfflineFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_offline_folder_downsync().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_offline_folder_downsync_finish (CamelOfflineFolder *folder,
                                      GAsyncResult *result,
                                      GError **error)
{
	g_return_val_if_fail (CAMEL_IS_OFFLINE_FOLDER (folder), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, folder), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_offline_folder_downsync), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

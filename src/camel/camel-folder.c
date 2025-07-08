/* camel-folder.c: Abstract class for an email folder
 *
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
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 */

#include "evolution-data-server-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-enumtypes.h"
#include "camel-file-utils.h"
#include "camel-filter-driver.h"
#include "camel-folder.h"
#include "camel-mempool.h"
#include "camel-mime-message.h"
#include "camel-network-service.h"
#include "camel-offline-store.h"
#include "camel-operation.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-store-search.h"
#include "camel-store-settings.h"
#include "camel-vtrash-folder.h"
#include "camel-string-utils.h"

#define d(x)
#define w(x)

typedef struct _AsyncContext AsyncContext;
typedef struct _SignalClosure SignalClosure;
typedef struct _FolderFilterData FolderFilterData;
typedef struct _FolderStateMapping FolderStateMapping;
typedef struct _CamelFolderClassPrivate CamelFolderClassPrivate;

struct _CamelFolderPrivate {
	CamelFolderSummary *summary;
	CamelFolderFlags folder_flags;

	GRecMutex lock;
	GMutex change_lock;
	/* must require the 'change_lock' to access this */
	gint frozen;
	CamelFolderChangeInfo *changed_frozen; /* queues changed events */
	gboolean skip_folder_lock;

	/* Changes to be emitted from an idle callback. */
	CamelFolderChangeInfo *pending_changes;

	gpointer parent_store;  /* weak pointer */

	GMutex property_lock;

	gchar *full_name;
	gchar *display_name;
	gchar *description;
	gchar *state_filename;

	CamelThreeState mark_seen;
	gint mark_seen_timeout;

	GMutex store_changes_lock;
	guint store_changes_id;
	gboolean store_changes_after_frozen;
};

struct _AsyncContext {
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	CamelFolder *destination;
	GPtrArray *message_uids;
	gchar *message_uid;
	gboolean delete_originals;
	gboolean expunge;
	gchar *start_uid;
	gchar *end_uid;

	/* results */
	GPtrArray *transferred_uids;
};

struct _CamelFolderChangeInfoPrivate {
	GHashTable *uid_stored;	/* what we have stored, which array they're in */
	GHashTable *uid_source;	/* used to create unique lists */
	GPtrArray  *uid_filter; /* uids to be filtered */
	CamelMemPool *uid_pool;	/* pool used to store copies of uid strings */
};

struct _SignalClosure {
	CamelFolder *folder;
	gchar *folder_name;
};

struct _FolderFilterData {
	GPtrArray *recents;
	GPtrArray *junk;
	GPtrArray *notjunk;
	CamelFolder *folder;
	CamelFilterDriver *driver;
};

struct _FolderStateMapping {
	const gchar *prop_name;
	guint32 tag;
};

struct _CamelFolderClassPrivate {
	FolderStateMapping state_mapping[10];
	gint n_state_mapping;
};

enum {
	PROP_0,
	PROP_DESCRIPTION,
	PROP_DISPLAY_NAME,
	PROP_FULL_NAME,
	PROP_PARENT_STORE,
	PROP_MARK_SEEN,
	PROP_MARK_SEEN_TIMEOUT,
	N_PROPS
};

enum {
	CHANGED,
	DELETED,
	RENAMED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (CamelFolder, camel_folder, G_TYPE_OBJECT,
				  G_ADD_PRIVATE (CamelFolder)
				  g_type_add_class_private (g_define_type_id, sizeof (CamelFolderClassPrivate)))

G_DEFINE_BOXED_TYPE (CamelFolderQuotaInfo,
		camel_folder_quota_info,
		camel_folder_quota_info_clone,
		camel_folder_quota_info_free)

/* Legacy state file for binary data.
 *
 * version:uint32
 *
 * Version 0 of the file:
 *
 * version:uint32 = 0
 * count:uint32				-- count of meta-data items
 * ( name:string value:string ) *count		-- meta-data items
 *
 * Version 1 of the file adds:
 * count:uint32					-- count of persistent properties
 * ( tag:uing32 value:tagtype ) *count		-- persistent properties
 */

#define CAMEL_OBJECT_STATE_FILE_MAGIC "CLMD"

enum camel_arg_t {
	CAMEL_ARG_TYPE = 0xf0000000, /* type field for tags */
	CAMEL_ARG_TAG = 0x0fffffff, /* tag field for args */

	CAMEL_ARG_INT = 0x10000000, /* gint */
	CAMEL_ARG_BOO = 0x50000000, /* bool */
	CAMEL_ARG_3ST = 0x60000000, /* three-state */
	CAMEL_ARG_I64 = 0x70000000  /* gint64 */
};

#define CAMEL_ARGV_MAX (20)

static void
folder_store_changes_job_cb (CamelSession *session,
			     GCancellable *cancellable,
			     gpointer user_data,
			     GError **error)
{
	CamelFolder *folder = user_data;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	camel_folder_synchronize_sync (folder, FALSE, cancellable, error);
}

static void
folder_schedule_store_changes_job (CamelFolder *folder)
{
	CamelSession *session;
	CamelStore *parent_store;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	parent_store = camel_folder_get_parent_store (folder);
	session = parent_store ? camel_service_ref_session (CAMEL_SERVICE (parent_store)) : NULL;
	if (session) {
		gchar *description;

		/* Translators: The first “%s” is replaced with an account name and the second “%s”
		   is replaced with a full path name. The spaces around “:” are intentional, as
		   the whole “%s : %s” is meant as an absolute identification of the folder. */
		description = g_strdup_printf (_("Storing changes in folder “%s : %s”"),
			camel_service_get_display_name (CAMEL_SERVICE (parent_store)),
			camel_folder_get_full_display_name (folder));

		camel_session_submit_job (session, description,
			folder_store_changes_job_cb,
			g_object_ref (folder), g_object_unref);

		g_free (description);
	}

	g_clear_object (&session);
}

static gboolean
folder_schedule_store_changes_job_cb (gpointer user_data)
{
	GWeakRef *weak_ref = user_data;
	GSource *source;
	CamelFolder *folder;

	source = g_main_current_source ();

	if (g_source_is_destroyed (source))
		return FALSE;

	folder = g_weak_ref_get (weak_ref);
	if (folder) {
		g_mutex_lock (&folder->priv->store_changes_lock);

		if (folder->priv->store_changes_id == g_source_get_id (source)) {
			folder->priv->store_changes_id = 0;
			folder_schedule_store_changes_job (folder);
		}

		g_mutex_unlock (&folder->priv->store_changes_lock);

		g_object_unref (folder);
	}

	return FALSE;
}

static void
folder_maybe_schedule_folder_change_store (CamelFolder *folder)
{
	CamelStore *store;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	g_mutex_lock (&folder->priv->store_changes_lock);

	if (folder->priv->store_changes_id)
		g_source_remove (folder->priv->store_changes_id);
	folder->priv->store_changes_id = 0;
	folder->priv->store_changes_after_frozen = FALSE;

	if (camel_folder_is_frozen (folder)) {
		folder->priv->store_changes_after_frozen = TRUE;
		g_mutex_unlock (&folder->priv->store_changes_lock);

		return;
	}

	store = camel_folder_get_parent_store (folder);

	if (store && camel_store_get_can_auto_save_changes (store)) {
		CamelSettings *settings;
		gint interval = -1;

		settings = camel_service_ref_settings (CAMEL_SERVICE (store));
		if (settings && CAMEL_IS_STORE_SETTINGS (settings))
			interval = camel_store_settings_get_store_changes_interval (CAMEL_STORE_SETTINGS (settings));
		g_clear_object (&settings);

		if (interval == 0)
			folder_schedule_store_changes_job (folder);
		else if (interval > 0)
			folder->priv->store_changes_id = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, interval,
				folder_schedule_store_changes_job_cb,
				camel_utils_weak_ref_new (folder), (GDestroyNotify) camel_utils_weak_ref_free);
	}

	g_mutex_unlock (&folder->priv->store_changes_lock);
}

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->message != NULL)
		g_object_unref (async_context->message);

	g_clear_object (&async_context->info);

	if (async_context->destination != NULL)
		g_object_unref (async_context->destination);

	g_clear_pointer (&async_context->message_uids, g_ptr_array_unref);
	g_clear_pointer (&async_context->transferred_uids, g_ptr_array_unref);
	g_free (async_context->message_uid);
	g_free (async_context->start_uid);
	g_free (async_context->end_uid);

	g_slice_free (AsyncContext, async_context);
}

static void
signal_closure_free (SignalClosure *signal_closure)
{
	g_clear_object (&signal_closure->folder);

	g_free (signal_closure->folder_name);

	g_slice_free (SignalClosure, signal_closure);
}

static gboolean
folder_emit_changed_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	CamelFolder *folder;
	CamelFolderChangeInfo *changes;

	folder = signal_closure->folder;

	g_mutex_lock (&folder->priv->change_lock);
	changes = folder->priv->pending_changes;
	folder->priv->pending_changes = NULL;
	g_mutex_unlock (&folder->priv->change_lock);

	g_signal_emit (folder, signals[CHANGED], 0, changes);

	if (changes && changes->uid_changed && changes->uid_changed->len > 0)
		folder_maybe_schedule_folder_change_store (folder);

	camel_folder_change_info_free (changes);

	return FALSE;
}

static gboolean
folder_emit_deleted_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;

	g_signal_emit (signal_closure->folder, signals[DELETED], 0);

	return FALSE;
}

static gboolean
folder_emit_renamed_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;

	g_signal_emit (
		signal_closure->folder,
		signals[RENAMED], 0,
		signal_closure->folder_name);

	return FALSE;
}

static gpointer
folder_filter_data_free_thread (gpointer user_data)
{
	FolderFilterData *data = user_data;

	g_return_val_if_fail (data != NULL, NULL);

	if (data->driver != NULL)
		g_object_unref (data->driver);
	if (data->recents != NULL)
		g_ptr_array_unref (data->recents);
	if (data->junk != NULL)
		g_ptr_array_unref (data->junk);
	if (data->notjunk != NULL)
		g_ptr_array_unref (data->notjunk);

	/* XXX Too late to pass a GError here. */
	camel_folder_summary_save (camel_folder_get_folder_summary (data->folder), NULL);

	camel_folder_thaw (data->folder);
	g_object_unref (data->folder);

	g_slice_free (FolderFilterData, data);

	return NULL;
}

static void
prepare_folder_filter_data_free (FolderFilterData *data)
{
	GThread *thread;

	/* Do the actual free in a dedicated thread, because the driver or
	 * folder unref can do network/blocking I/O operations, but this
	 * function is called in the main (UI) thread.
	*/
	thread = g_thread_new (NULL, folder_filter_data_free_thread, data);
	g_thread_unref (thread);
}

static void
folder_filter (CamelSession *session,
               GCancellable *cancellable,
               FolderFilterData *data,
               GError **error)
{
	CamelMessageInfo *info;
	CamelStore *parent_store;
	gint i;
	CamelJunkFilter *junk_filter;
	gboolean synchronize = FALSE;
	const gchar *full_name, *full_display_name;

	full_name = camel_folder_get_full_name (data->folder);
	full_display_name = camel_folder_get_full_display_name (data->folder);
	parent_store = camel_folder_get_parent_store (data->folder);
	junk_filter = camel_session_get_junk_filter (session);

	/* Keep the junk filter alive until we're done. */
	if (junk_filter != NULL)
		g_object_ref (junk_filter);

	/* Reset junk learn flag so that we don't process it again */
	if (data->junk) {
		CamelFolderSummary *summary;

		summary = camel_folder_get_folder_summary (data->folder);

		camel_folder_summary_lock (summary);

		for (i = 0; i < data->junk->len; i++) {
			info = camel_folder_summary_get (summary, data->junk->pdata[i]);

			/* The flag can be unset by another thread - recheck it again, to avoid repeated junk learn */
			if (!info || !camel_message_info_set_flags (info, CAMEL_MESSAGE_JUNK_LEARN, 0)) {
				g_ptr_array_remove_index_fast (data->junk, i);
				i--;
			}
			g_clear_object (&info);
		}

		camel_folder_summary_unlock (summary);
	}

	if (data->notjunk) {
		CamelFolderSummary *summary;

		summary = camel_folder_get_folder_summary (data->folder);

		camel_folder_summary_lock (summary);

		for (i = 0; i < data->notjunk->len; i++) {
			info = camel_folder_summary_get (summary, data->notjunk->pdata[i]);

			/* The flag can be unset by another thread - recheck it again, to avoid repeated not-junk learn */
			if (!info || !camel_message_info_set_flags (info, CAMEL_MESSAGE_JUNK_LEARN, 0)) {
				g_ptr_array_remove_index_fast (data->notjunk, i);
				i--;
			}
			g_clear_object (&info);
		}

		camel_folder_summary_unlock (summary);
	}

	if (data->junk && data->junk->len > 0) {
		gboolean success = TRUE;

		camel_operation_push_message (
			cancellable, dngettext (GETTEXT_PACKAGE,
			/* Translators: The first “%s” is replaced with an account name and the second “%s”
			   is replaced with a full path name. The spaces around “:” are intentional, as
			   the whole “%s : %s” is meant as an absolute identification of the folder. */
			"Learning new spam message in “%s : %s”",
			"Learning new spam messages in “%s : %s”",
			data->junk->len),
			camel_service_get_display_name (CAMEL_SERVICE (parent_store)),
			full_display_name);

		for (i = 0; success && i < data->junk->len; i++) {
			CamelMimeMessage *message;
			gint pc = 100 * i / data->junk->len;

			if (g_cancellable_set_error_if_cancelled (
				cancellable, error))
				break;

			message = camel_folder_get_message_sync (
				data->folder, data->junk->pdata[i],
				cancellable, error);

			if (message == NULL)
				break;

			camel_operation_progress (cancellable, pc);
			success = camel_junk_filter_learn_junk (
				junk_filter, message, cancellable, error);
			g_object_unref (message);

			synchronize |= success;
		}

		camel_operation_pop_message (cancellable);
	}

	if (error && *error)
		goto exit;

	if (data->notjunk && data->notjunk->len > 0) {
		gboolean success = TRUE;

		camel_operation_push_message (
			cancellable, dngettext (GETTEXT_PACKAGE,
			/* Translators: The first “%s” is replaced with an account name and the second “%s”
			   is replaced with a full path name. The spaces around “:” are intentional, as
			   the whole “%s : %s” is meant as an absolute identification of the folder. */
			"Learning new ham message in “%s : %s”",
			"Learning new ham messages in “%s : %s”",
			data->notjunk->len),
			camel_service_get_display_name (CAMEL_SERVICE (parent_store)),
			full_display_name);

		for (i = 0; success && i < data->notjunk->len; i++) {
			CamelMimeMessage *message;
			gint pc = 100 * i / data->notjunk->len;

			if (g_cancellable_set_error_if_cancelled (
				cancellable, error))
				break;

			message = camel_folder_get_message_sync (
				data->folder, data->notjunk->pdata[i],
				cancellable, error);

			if (message == NULL)
				break;

			camel_operation_progress (cancellable, pc);
			success = camel_junk_filter_learn_not_junk (
				junk_filter, message, cancellable, error);
			g_object_unref (message);

			synchronize |= success;
		}

		camel_operation_pop_message (cancellable);
	}

	if (error && *error)
		goto exit;

	if (synchronize)
		camel_junk_filter_synchronize (
			junk_filter, cancellable, error);

	if (error && *error)
		goto exit;

	if (data->driver && data->recents) {
		camel_operation_push_message (
			cancellable, dngettext (GETTEXT_PACKAGE,
			/* Translators: The first “%s” is replaced with an account name and the second “%s”
			   is replaced with a full path name. The spaces around “:” are intentional, as
			   the whole “%s : %s” is meant as an absolute identification of the folder. */
			"Filtering new message in “%s : %s”",
			"Filtering new messages in “%s : %s”",
			data->recents->len),
			camel_service_get_display_name (CAMEL_SERVICE (parent_store)),
			full_display_name);

		camel_filter_driver_log_info (data->driver, "\nReported %d recent messages in '%s : %s'",
			data->recents->len, camel_service_get_display_name (CAMEL_SERVICE (parent_store)), full_name);

		camel_filter_driver_filter_folder (data->driver, data->folder, NULL, data->recents, FALSE, cancellable, error);

		camel_operation_pop_message (cancellable);

		camel_filter_driver_flush (data->driver, error);

		/* Save flag/info changes made by the filter */
		if (error && !*error)
			camel_folder_synchronize_sync (data->folder, FALSE, cancellable, error);

	} else if (data->driver) {
		camel_filter_driver_log_info (data->driver, "No recent messages reported in '%s : %s'",
			camel_service_get_display_name (CAMEL_SERVICE (parent_store)), full_name);
	}

exit:
	if (junk_filter != NULL)
		g_object_unref (junk_filter);
}

static gint
cmp_array_uids (gconstpointer a,
                gconstpointer b,
                gpointer user_data)
{
	const gchar *uid1 = *(const gchar **) a;
	const gchar *uid2 = *(const gchar **) b;
	CamelFolder *folder = user_data;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	return camel_folder_cmp_uids (folder, uid1, uid2);
}

static void
folder_transfer_message_to (CamelFolder *source,
                            const gchar *uid,
                            CamelFolder *dest,
                            gchar **transferred_uid,
                            gboolean delete_original,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelMimeMessage *msg;
	CamelMessageInfo *minfo, *info;
	guint32 source_folder_flags;
	GError *local_error = NULL;

	/* Default implementation. */

	msg = camel_folder_get_message_sync (source, uid, cancellable, error);
	if (!msg)
		return;

	source_folder_flags = camel_folder_get_flags (source);

	/* if its deleted we poke the flags, so we need to copy the messageinfo */
	if ((source_folder_flags & CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY)
	    && (minfo = camel_folder_get_message_info (source, uid))) {
		info = camel_message_info_clone (minfo, NULL);
		g_clear_object (&minfo);
	} else {
		info = camel_message_info_new_from_message (NULL, msg);
	}

	/* unset deleted flag when transferring from trash folder */
	if ((source_folder_flags & CAMEL_FOLDER_IS_TRASH) != 0)
		camel_message_info_set_flags (info, CAMEL_MESSAGE_DELETED, 0);
	/* unset junk flag when transferring from junk folder */
	if ((source_folder_flags & CAMEL_FOLDER_IS_JUNK) != 0)
		camel_message_info_set_flags (info, CAMEL_MESSAGE_JUNK, 0);

	camel_folder_append_message_sync (
		dest, msg, info, transferred_uid,
		cancellable, &local_error);
	g_object_unref (msg);

	if (local_error != NULL)
		g_propagate_error (error, local_error);
	else if (delete_original)
		camel_folder_set_message_flags (
			source, uid, CAMEL_MESSAGE_DELETED |
			CAMEL_MESSAGE_SEEN, ~0);

	g_clear_object (&info);
}

static gboolean
folder_maybe_connect_sync (CamelFolder *folder,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelService *service;
	CamelStore *parent_store;
	CamelServiceConnectionStatus status;
	CamelSession *session;
	gboolean connect = FALSE;
	gboolean success = TRUE;

	/* This is meant to recover from dropped connections
	 * when the CamelService is online but disconnected. */

	parent_store = camel_folder_get_parent_store (folder);

	service = CAMEL_SERVICE (parent_store);
	session = camel_service_ref_session (service);
	status = camel_service_get_connection_status (service);
	connect = session && camel_session_get_online (session) && (status != CAMEL_SERVICE_CONNECTED);
	g_clear_object (&session);

	if (connect && CAMEL_IS_NETWORK_SERVICE (parent_store)) {
		/* Disregard errors here.  Just want to
		 * know whether to attempt a connection. */
		connect = camel_network_service_can_reach_sync (
			CAMEL_NETWORK_SERVICE (parent_store),
			cancellable, NULL);
	}

	if (connect && CAMEL_IS_OFFLINE_STORE (parent_store)) {
		CamelOfflineStore *offline_store;

		offline_store = CAMEL_OFFLINE_STORE (parent_store);
		if (!camel_offline_store_get_online (offline_store))
			connect = FALSE;
	}

	if (connect) {
		success = camel_service_connect_sync (
			service, cancellable, error);
	}

	return success;
}

static void
folder_set_parent_store (CamelFolder *folder,
                         CamelStore *parent_store)
{
	g_return_if_fail (CAMEL_IS_STORE (parent_store));
	g_return_if_fail (folder->priv->parent_store == NULL);

	folder->priv->parent_store = parent_store;

	g_object_add_weak_pointer (
		G_OBJECT (parent_store), &folder->priv->parent_store);
}

static void
folder_set_property (GObject *object,
                     guint property_id,
                     const GValue *value,
                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DESCRIPTION:
			camel_folder_set_description (
				CAMEL_FOLDER (object),
				g_value_get_string (value));
			return;

		case PROP_DISPLAY_NAME:
			camel_folder_set_display_name (
				CAMEL_FOLDER (object),
				g_value_get_string (value));
			return;

		case PROP_FULL_NAME:
			camel_folder_set_full_name (
				CAMEL_FOLDER (object),
				g_value_get_string (value));
			return;

		case PROP_PARENT_STORE:
			folder_set_parent_store (
				CAMEL_FOLDER (object),
				g_value_get_object (value));
			return;

		case PROP_MARK_SEEN:
			camel_folder_set_mark_seen (
				CAMEL_FOLDER (object),
				g_value_get_enum (value));
			return;

		case PROP_MARK_SEEN_TIMEOUT:
			camel_folder_set_mark_seen_timeout (
				CAMEL_FOLDER (object),
				g_value_get_int (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
folder_get_property (GObject *object,
                     guint property_id,
                     GValue *value,
                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DESCRIPTION:
			g_value_take_string (
				value, camel_folder_dup_description (
				CAMEL_FOLDER (object)));
			return;

		case PROP_DISPLAY_NAME:
			g_value_take_string (
				value, camel_folder_dup_display_name (
				CAMEL_FOLDER (object)));
			return;

		case PROP_FULL_NAME:
			g_value_take_string (
				value, camel_folder_dup_full_name (
				CAMEL_FOLDER (object)));
			return;

		case PROP_PARENT_STORE:
			g_value_set_object (
				value, camel_folder_get_parent_store (
				CAMEL_FOLDER (object)));
			return;

		case PROP_MARK_SEEN:
			g_value_set_enum (
				value, camel_folder_get_mark_seen (
				CAMEL_FOLDER (object)));
			return;

		case PROP_MARK_SEEN_TIMEOUT:
			g_value_set_int (
				value, camel_folder_get_mark_seen_timeout (
				CAMEL_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
folder_dispose (GObject *object)
{
	CamelFolder *folder;

	folder = CAMEL_FOLDER (object);

	g_mutex_lock (&folder->priv->store_changes_lock);
	if (folder->priv->store_changes_id)
		g_source_remove (folder->priv->store_changes_id);
	folder->priv->store_changes_id = 0;
	g_mutex_unlock (&folder->priv->store_changes_lock);

	if (folder->priv->summary) {
		camel_folder_summary_save (folder->priv->summary, NULL);
		g_clear_object (&folder->priv->summary);
	}

	if (folder->priv->parent_store != NULL) {
		g_object_remove_weak_pointer (
			G_OBJECT (folder->priv->parent_store),
			&folder->priv->parent_store);
		folder->priv->parent_store = NULL;
	}

	/* Chain up to parent's dispose () method. */
	G_OBJECT_CLASS (camel_folder_parent_class)->dispose (object);
}

static void
folder_finalize (GObject *object)
{
	CamelFolderPrivate *priv;

	priv = CAMEL_FOLDER (object)->priv;

	g_mutex_clear (&priv->property_lock);

	g_free (priv->full_name);
	g_free (priv->display_name);
	g_free (priv->description);
	g_free (priv->state_filename);

	camel_folder_change_info_free (priv->changed_frozen);

	if (priv->pending_changes != NULL)
		camel_folder_change_info_free (priv->pending_changes);

	g_rec_mutex_clear (&priv->lock);
	g_mutex_clear (&priv->change_lock);
	g_mutex_clear (&priv->store_changes_lock);

	/* Chain up to parent's finalize () method. */
	G_OBJECT_CLASS (camel_folder_parent_class)->finalize (object);
}

static gint
folder_get_message_count (CamelFolder *folder)
{
	g_return_val_if_fail (folder->priv->summary != NULL, -1);

	return camel_folder_summary_count (folder->priv->summary);
}

static guint32
folder_get_permanent_flags (CamelFolder *folder)
{
	return 0;
}

static guint32
folder_get_message_flags (CamelFolder *folder,
                          const gchar *uid)
{
	CamelMessageInfo *info;
	guint32 flags;

	g_return_val_if_fail (folder->priv->summary != NULL, 0);

	info = camel_folder_summary_get (folder->priv->summary, uid);
	if (info == NULL)
		return 0;

	flags = camel_message_info_get_flags (info);
	g_clear_object (&info);

	return flags;
}

static gboolean
folder_set_message_flags (CamelFolder *folder,
                          const gchar *uid,
                          guint32 mask,
                          guint32 set)
{
	CamelMessageInfo *info;
	gint res;

	g_return_val_if_fail (folder->priv->summary != NULL, FALSE);

	camel_folder_summary_lock (folder->priv->summary);

	info = camel_folder_summary_get (folder->priv->summary, uid);
	if (info == NULL) {
		camel_folder_summary_unlock (folder->priv->summary);
		return FALSE;
	}

	res = camel_message_info_set_flags (info, mask, set);
	g_clear_object (&info);

	camel_folder_summary_unlock (folder->priv->summary);

	return res;
}

static GPtrArray *
folder_dup_uids (CamelFolder *folder)
{
	g_return_val_if_fail (folder->priv->summary != NULL, NULL);

	return camel_folder_summary_dup_uids (folder->priv->summary);
}

static GPtrArray *
folder_dup_uncached_uids (CamelFolder *folder,
                          GPtrArray *uids,
                          GError **error)
{
	GPtrArray *result;
	gint i;

	result = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);

	g_ptr_array_set_size (result, uids->len);
	for (i = 0; i < uids->len; i++)
		result->pdata[i] =
			(gpointer) camel_pstring_strdup (uids->pdata[i]);

	return result;
}

static gint
folder_cmp_uids (CamelFolder *folder,
                 const gchar *uid1,
                 const gchar *uid2)
{
	g_return_val_if_fail (uid1 != NULL, 0);
	g_return_val_if_fail (uid2 != NULL, 0);

	return g_ascii_strtoull (uid1, NULL, 10) - g_ascii_strtoull (uid2, NULL, 10);
}

static void
folder_sort_uids (CamelFolder *folder,
                  GPtrArray *uids)
{
	g_qsort_with_data (
		uids->pdata, uids->len,
		sizeof (gpointer), cmp_array_uids, folder);
}

static gboolean
folder_search_sync (CamelFolder *folder,
		    const gchar *expression,
		    GPtrArray **out_uids,
		    GCancellable *cancellable,
		    GError **error)
{
	CamelStore *parent_store;
	CamelStoreSearch *store_search;
	gboolean success = FALSE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (expression != NULL, FALSE);
	g_return_val_if_fail (out_uids != NULL, FALSE);

	*out_uids = NULL;

	parent_store = camel_folder_get_parent_store (folder);
	if (!parent_store)
		return FALSE;

	store_search = camel_store_search_new (parent_store);
	camel_store_search_set_expression (store_search, expression);
	camel_store_search_add_folder (store_search, folder);

	if (camel_store_search_rebuild_sync (store_search, cancellable, error)) {
		CamelMatchThreadsKind threads_kind;
		CamelFolderThreadFlags threads_flags = CAMEL_FOLDER_THREAD_FLAG_NONE;
		gboolean read_result = TRUE;

		threads_kind = camel_store_search_get_match_threads_kind (store_search, &threads_flags);
		if (threads_kind != CAMEL_MATCH_THREADS_KIND_NONE) {
			GPtrArray *items = NULL;

			if (camel_store_search_add_match_threads_items_sync (store_search, &items, cancellable, error) && items) {
				CamelStoreSearchIndex *index;

				index = camel_store_search_ref_result_index (store_search);
				camel_store_search_index_apply_match_threads (index, items, threads_kind, threads_flags, cancellable);
				camel_store_search_set_result_index (store_search, index);

				g_clear_pointer (&index, camel_store_search_index_unref);
				g_clear_pointer (&items, g_ptr_array_unref);
			}
		}

		if (read_result)
			success = camel_store_search_get_uids_sync (store_search, camel_folder_get_full_name (folder), out_uids, cancellable, error);
	}

	g_object_unref (store_search);

	return success;
}

static CamelMessageInfo *
folder_get_message_info (CamelFolder *folder,
                         const gchar *uid)
{
	g_return_val_if_fail (folder->priv->summary != NULL, NULL);

	return camel_folder_summary_get (folder->priv->summary, uid);
}

static void
folder_delete (CamelFolder *folder)
{
	if (folder->priv->summary)
		camel_folder_summary_clear (folder->priv->summary, NULL);
}

static void
folder_rename (CamelFolder *folder,
               const gchar *new)
{
	gchar *tmp;

	d (printf ("CamelFolder:rename ('%s')\n", new));

	camel_folder_set_full_name (folder, new);

	tmp = strrchr (new, '/');
	camel_folder_set_display_name (folder, (tmp != NULL) ? tmp + 1 : new);
}

static void
folder_freeze (CamelFolder *folder)
{
	g_return_if_fail (folder->priv->frozen >= 0);

	g_mutex_lock (&folder->priv->change_lock);

	folder->priv->frozen++;
	if (folder->priv->summary)
		g_object_freeze_notify (G_OBJECT (folder->priv->summary));

	d (printf ("freeze (%p '%s') = %d\n", folder, folder->full_name, folder->priv->frozen));
	g_mutex_unlock (&folder->priv->change_lock);
}

static void
folder_thaw (CamelFolder *folder)
{
	CamelFolderChangeInfo *info = NULL;

	g_return_if_fail (folder->priv->frozen > 0);

	g_mutex_lock (&folder->priv->change_lock);

	folder->priv->frozen--;
	if (folder->priv->summary)
		g_object_thaw_notify (G_OBJECT (folder->priv->summary));

	d (printf ("thaw (%p '%s') = %d\n", folder, folder->full_name, folder->priv->frozen));

	if (folder->priv->frozen == 0
	    && camel_folder_change_info_changed (folder->priv->changed_frozen)) {
		info = folder->priv->changed_frozen;
		folder->priv->changed_frozen = camel_folder_change_info_new ();
	}

	g_mutex_unlock (&folder->priv->change_lock);

	if (info) {
		camel_folder_changed (folder, info);
		camel_folder_change_info_free (info);

		if (folder->priv->summary)
			camel_folder_summary_save (folder->priv->summary, NULL);
	}

	if (!camel_folder_is_frozen (folder)) {
		g_mutex_lock (&folder->priv->store_changes_lock);
		if (folder->priv->store_changes_after_frozen) {
			folder->priv->store_changes_after_frozen = FALSE;
			g_mutex_unlock (&folder->priv->store_changes_lock);

			folder_maybe_schedule_folder_change_store (folder);
		} else {
			g_mutex_unlock (&folder->priv->store_changes_lock);
		}
	}
}

static gboolean
folder_is_frozen (CamelFolder *folder)
{
	return folder->priv->frozen != 0;
}

static gboolean
folder_refresh_info_sync (CamelFolder *folder,
                          GCancellable *cancellable,
                          GError **error)
{
	return TRUE;
}

static gboolean
folder_transfer_messages_to_sync (CamelFolder *source,
                                  GPtrArray *uids,
                                  CamelFolder *dest,
                                  gboolean delete_originals,
                                  GPtrArray **transferred_uids,
                                  GCancellable *cancellable,
                                  GError **error)
{
	gchar **ret_uid = NULL;
	gint i;
	GError *local_error = NULL;
	GCancellable *local_cancellable = camel_operation_new ();
	gulong handler_id = 0;

	if (transferred_uids) {
		*transferred_uids = g_ptr_array_new_with_free_func (g_free);
		g_ptr_array_set_size (*transferred_uids, uids->len);
	}

	/* to not propagate status messages from sub-functions into UI */
	if (cancellable)
		handler_id = g_signal_connect_swapped (cancellable, "cancelled", G_CALLBACK (g_cancellable_cancel), local_cancellable);

	if (delete_originals)
		camel_operation_push_message (
			cancellable, _("Moving messages"));
	else
		camel_operation_push_message (
			cancellable, _("Copying messages"));

	if (uids->len > 1) {
		camel_folder_freeze (dest);
		if (delete_originals)
			camel_folder_freeze (source);
	}

	for (i = 0; i < uids->len && local_error == NULL; i++) {
		if (transferred_uids)
			ret_uid = (gchar **) &((*transferred_uids)->pdata[i]);
		folder_transfer_message_to (
			source, uids->pdata[i], dest, ret_uid,
			delete_originals, local_cancellable, &local_error);
		camel_operation_progress (
			cancellable, i * 100 / uids->len);
	}

	if (uids->len > 1) {
		camel_folder_thaw (dest);
		if (delete_originals)
			camel_folder_thaw (source);
	}

	camel_operation_pop_message (cancellable);

	if (local_error != NULL)
		g_propagate_error (error, local_error);
	g_object_unref (local_cancellable);
	if (cancellable)
		g_signal_handler_disconnect (cancellable, handler_id);

	return TRUE;
}

static CamelFolderQuotaInfo *
folder_get_quota_info_sync (CamelFolder *folder,
                            GCancellable *cancellable,
                            GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		/* Translators: The first “%s” is replaced with an account name and the second “%s”
		   is replaced with a full path name. The spaces around “:” are intentional, as
		   the whole “%s : %s” is meant as an absolute identification of the folder. */
		_("Quota information not supported for folder “%s : %s”"),
		camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))),
		camel_folder_get_full_display_name (folder));

	return NULL;
}

/* Signal callback that stops emission when folder is frozen. */
static void
folder_changed (CamelFolder *folder,
                CamelFolderChangeInfo *info)
{
	CamelStore *parent_store;
	struct _CamelFolderChangeInfoPrivate *p = info->priv;
	CamelSession *session;
	CamelFilterDriver *driver = NULL;
	CamelJunkFilter *junk_filter;
	GPtrArray *junk = NULL;
	GPtrArray *notjunk = NULL;
	GPtrArray *recents = NULL;
	gint i;

	g_return_if_fail (info != NULL);

	g_mutex_lock (&folder->priv->change_lock);
	if (folder->priv->frozen) {
		camel_folder_change_info_cat (folder->priv->changed_frozen, info);
		g_mutex_unlock (&folder->priv->change_lock);
		g_signal_stop_emission (folder, signals[CHANGED], 0);
		return;
	}
	g_mutex_unlock (&folder->priv->change_lock);

	parent_store = camel_folder_get_parent_store (folder);
	if (!parent_store)
		return;

	session = camel_service_ref_session (CAMEL_SERVICE (parent_store));
	if (!session)
		return;

	junk_filter = camel_session_get_junk_filter (session);

	if (junk_filter != NULL && info->uid_changed->len) {
		guint32 flags;

		for (i = 0; i < info->uid_changed->len; i++) {
			flags = camel_folder_summary_get_info_flags (folder->priv->summary, info->uid_changed->pdata[i]);
			if (flags != (~0) && (flags & CAMEL_MESSAGE_JUNK_LEARN) != 0) {
				if (flags & CAMEL_MESSAGE_JUNK) {
					if (!junk)
						junk = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
					g_ptr_array_add (junk, (gpointer) camel_pstring_strdup (info->uid_changed->pdata[i]));
				} else {
					if (!notjunk)
						notjunk = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
					g_ptr_array_add (notjunk, (gpointer) camel_pstring_strdup (info->uid_changed->pdata[i]));
				}

				/* the flag will be unset in the thread, to not block the UI/main thread */
			}
		}
	}

	if ((camel_folder_get_flags (folder) & (CAMEL_FOLDER_FILTER_RECENT | CAMEL_FOLDER_FILTER_JUNK))
	    && p->uid_filter->len > 0)
		driver = camel_session_get_filter_driver (
			session,
			(camel_folder_get_flags (folder) & CAMEL_FOLDER_FILTER_RECENT)
			? "incoming" : "junktest", folder, NULL);

	if (driver) {
		recents = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
		for (i = 0; i < p->uid_filter->len; i++)
			g_ptr_array_add (recents, (gpointer) camel_pstring_strdup (p->uid_filter->pdata[i]));

		g_ptr_array_set_size (p->uid_filter, 0);
	}

	if (driver || junk || notjunk) {
		FolderFilterData *data;
		gchar *description;

		data = g_slice_new0 (FolderFilterData);
		data->recents = recents;
		data->junk = junk;
		data->notjunk = notjunk;
		data->folder = g_object_ref (folder);
		data->driver = driver;

		camel_folder_freeze (folder);

		/* Copy changes back to changed_frozen list to retain
		 * them while we are filtering */
		g_mutex_lock (&folder->priv->change_lock);
		camel_folder_change_info_cat (
			folder->priv->changed_frozen, info);
		g_mutex_unlock (&folder->priv->change_lock);

		/* Translators: The first “%s” is replaced with an account name and the second “%s”
		   is replaced with a full path name. The spaces around “:” are intentional, as
		   the whole “%s : %s” is meant as an absolute identification of the folder. */
		description = g_strdup_printf (_("Filtering folder “%s : %s”"),
			camel_service_get_display_name (CAMEL_SERVICE (parent_store)),
			camel_folder_get_full_display_name (folder));

		camel_session_submit_job (
			session, description, (CamelSessionCallback) folder_filter,
			data, (GDestroyNotify) prepare_folder_filter_data_free);

		g_signal_stop_emission (folder, signals[CHANGED], 0);

		g_free (description);
	}

	g_object_unref (session);
}

/* Providers having different display names from the folder names can override
   the function to return appropriate display name. */
static const gchar *
folder_get_full_display_name (CamelFolder *folder)
{
	const gchar *res;

	g_mutex_lock (&folder->priv->property_lock);

	if (!strchr (folder->priv->full_name, '/'))
		res = folder->priv->display_name;
	else
		res = folder->priv->full_name;

	g_mutex_unlock (&folder->priv->property_lock);

	return res;
}

static gboolean
folder_dup_headers_sync (CamelFolder *folder,
			 const gchar *uid,
			 CamelNameValueArray **out_headers,
			 GCancellable *cancellable,
			 GError **error)
{
	gboolean success = FALSE;

	CamelFolderSummary *summary;

	summary = camel_folder_get_folder_summary (folder);
	if (summary) {
		CamelMessageInfo *nfo;

		nfo = camel_folder_summary_peek_loaded (summary, uid);
		if (nfo) {
			*out_headers = camel_message_info_dup_headers (nfo);
			success = *out_headers != NULL;
			g_object_unref (nfo);
		}
	}

	if (!success) {
		gchar *filename;

		filename = camel_folder_get_filename (folder, uid, NULL);
		if (filename) {
			GFile *file;
			GFileInputStream *input_stream;

			file = g_file_new_for_path (filename);
			input_stream = g_file_read (file, cancellable, NULL);

			if (input_stream) {
				CamelMimeParser *parser;

				parser = camel_mime_parser_new ();
				camel_mime_parser_init_with_input_stream (parser, G_INPUT_STREAM (input_stream));

				/* looking only for headers, thus parse only them, not the whole message, when
				   a file is available locally */
				if (camel_mime_parser_step (parser, NULL, NULL) != CAMEL_MIME_PARSER_STATE_EOF) {
					switch (camel_mime_parser_state (parser)) {
					case CAMEL_MIME_PARSER_STATE_HEADER:
					case CAMEL_MIME_PARSER_STATE_MESSAGE:
					case CAMEL_MIME_PARSER_STATE_MULTIPART:
						*out_headers = camel_mime_parser_dup_headers (parser);
						success = TRUE;
						break;
					default:
						break;
					}
				}

				g_clear_object (&parser);
			}

			g_clear_object (&file);
			g_clear_object (&input_stream);
			g_free (filename);
		}
	}

	if (!success && error && !*error) {
		g_set_error_literal (
			error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot get message headers, local message file not found"));
	}

	return success;
}

static gboolean
folder_search_header_sync (CamelFolder *folder,
			   const gchar *header_name,
			   /* const */ GPtrArray *words, /* gchar * */
			   GPtrArray **out_uids, /* gchar * */
			   GCancellable *cancellable,
			   GError **error)
{
	g_set_error_literal (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Cannot search message headers remotely, not supported"));

	return FALSE;
}

static gboolean
folder_search_body_sync (CamelFolder *folder,
			 /* const */ GPtrArray *words, /* gchar * */
			 GPtrArray **out_uids, /* gchar * */
			 GCancellable *cancellable,
			 GError **error)
{
	g_set_error_literal (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Cannot search message bodies remotely, not supported"));

	return FALSE;
}

static gboolean
folder_read_legacy_state (CamelFolder *self,
			  const gchar *state_filename)
{
	GValue gvalue = G_VALUE_INIT;
	CamelFolderClass *folder_class;
	CamelFolderClassPrivate *priv;
	guint32 count, version;
	guint ii, jj;
	FILE *fp;
	gchar magic[4];

	fp = g_fopen (state_filename, "rb");
	if (fp == NULL)
		return TRUE;

	if (fread (magic, 4, 1, fp) != 1
	    || memcmp (magic, CAMEL_OBJECT_STATE_FILE_MAGIC, 4) != 0) {
		fclose (fp);
		return FALSE;
	}

	if (camel_file_util_decode_uint32 (fp, &version) == -1) {
		fclose (fp);
		return FALSE;
	}

	if (version > 2) {
		fclose (fp);
		return FALSE;
	}

	if (camel_file_util_decode_uint32 (fp, &count) == -1) {
		fclose (fp);
		return FALSE;
	}

	/* XXX Camel no longer supports meta-data in state
	 *     files, so we're just eating dead data here. */
	for (ii = 0; ii < count; ii++) {
		gchar *name = NULL;
		gchar *value = NULL;
		gboolean success;

		success =
			camel_file_util_decode_string (fp, &name) == 0 &&
			camel_file_util_decode_string (fp, &value) == 0;

		g_free (name);
		g_free (value);

		if (!success) {
			fclose (fp);
			return FALSE;
		}
	}

	if (version == 0) {
		fclose (fp);
		return TRUE;
	}

	if (camel_file_util_decode_uint32 (fp, &count) == -1) {
		fclose (fp);
		return TRUE;
	}

	if (count == 0 || count > 1024) {
		/* Maybe it was just version 0 after all. */
		fclose (fp);
		return TRUE;
	}

	count = MIN (count, CAMEL_ARGV_MAX);

	folder_class = CAMEL_FOLDER_GET_CLASS (self);
	priv = G_TYPE_CLASS_GET_PRIVATE (folder_class, CAMEL_TYPE_FOLDER, CamelFolderClassPrivate);

	for (ii = 0; ii < count; ii++) {
		guint32 tag, v_uint32;
		gint32 v_int32;
		gint64 v_int64;

		if (camel_file_util_decode_uint32 (fp, &tag) == -1)
			goto exit;

		/* Record state file values into GValues. */
		switch (tag & CAMEL_ARG_TYPE) {
			case CAMEL_ARG_BOO:
				if (camel_file_util_decode_uint32 (fp, &v_uint32) == -1)
					goto exit;
				g_value_init (&gvalue, G_TYPE_BOOLEAN);
				g_value_set_boolean (&gvalue, (gboolean) v_uint32);
				break;
			case CAMEL_ARG_INT:
				if (camel_file_util_decode_fixed_int32 (fp, &v_int32) == -1)
					goto exit;
				g_value_init (&gvalue, G_TYPE_INT);
				g_value_set_int (&gvalue, v_int32);
				break;
			case CAMEL_ARG_3ST:
				if (camel_file_util_decode_uint32 (fp, &v_uint32) == -1)
					goto exit;
				g_value_init (&gvalue, CAMEL_TYPE_THREE_STATE);
				g_value_set_enum (&gvalue, (CamelThreeState) v_uint32);
				break;
			case CAMEL_ARG_I64:
				if (camel_file_util_decode_gint64 (fp, &v_int64) == -1)
					goto exit;
				g_value_init (&gvalue, G_TYPE_INT64);
				g_value_set_int64 (&gvalue, v_int64);
				break;
			default:
				g_warn_if_reached ();
				goto exit;
		}

		/* Now we have to match the legacy numeric CamelArg tag
		 * value with a GObject property. */

		tag &= CAMEL_ARG_TAG;  /* filter out the type code */

		for (jj = 0; jj < priv->n_state_mapping; jj++) {
			FolderStateMapping *fsm = &priv->state_mapping[jj];
			if (tag == fsm->tag) {
				g_object_set_property (G_OBJECT (self), fsm->prop_name, &gvalue);
				break;
			}
		}

		g_value_unset (&gvalue);
	}

 exit:
	fclose (fp);
	return TRUE;
}

static void
camel_folder_class_init (CamelFolderClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = folder_set_property;
	object_class->get_property = folder_get_property;
	object_class->dispose = folder_dispose;
	object_class->finalize = folder_finalize;

	class->get_message_count = folder_get_message_count;
	class->get_permanent_flags = folder_get_permanent_flags;
	class->get_message_flags = folder_get_message_flags;
	class->set_message_flags = folder_set_message_flags;
	class->dup_uids = folder_dup_uids;
	class->dup_uncached_uids = folder_dup_uncached_uids;
	class->cmp_uids = folder_cmp_uids;
	class->sort_uids = folder_sort_uids;
	class->search_sync = folder_search_sync;
	class->get_message_info = folder_get_message_info;
	class->delete_ = folder_delete;
	class->rename = folder_rename;
	class->freeze = folder_freeze;
	class->thaw = folder_thaw;
	class->is_frozen = folder_is_frozen;
	class->get_quota_info_sync = folder_get_quota_info_sync;
	class->refresh_info_sync = folder_refresh_info_sync;
	class->transfer_messages_to_sync = folder_transfer_messages_to_sync;
	class->changed = folder_changed;
	class->get_full_display_name = folder_get_full_display_name;
	class->dup_headers_sync = folder_dup_headers_sync;
	class->search_header_sync = folder_search_header_sync;
	class->search_body_sync = folder_search_body_sync;

	/**
	 * CamelFolder:description
	 *
	 * The folder's description.
	 **/
	properties[PROP_DESCRIPTION] =
		g_param_spec_string (
			"description", NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelFolder:display-name
	 *
	 * The folder's display name.
	 **/
	properties[PROP_DISPLAY_NAME] =
		g_param_spec_string (
			"display-name", NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelFolder:full-name
	 *
	 * The folder's fully qualified name.
	 **/
	properties[PROP_FULL_NAME] =
		g_param_spec_string (
			"full-name", NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelFolder:parent-store
	 *
	 * The #CamelStore to which the folder belongs.
	 **/
	properties[PROP_PARENT_STORE] =
		g_param_spec_object (
			"parent-store", NULL, NULL,
			CAMEL_TYPE_STORE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelFolder:mark-seen
	 *
	 * A #CamelThreeState persistent option of the folder,
	 * which can override global option to mark messages
	 * as seen after certain interval.
	 *
	 * Since: 3.32
	 **/
	properties[PROP_MARK_SEEN] =
		g_param_spec_enum (
			"mark-seen", NULL, NULL,
			CAMEL_TYPE_THREE_STATE,
			CAMEL_THREE_STATE_INCONSISTENT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			CAMEL_FOLDER_PARAM_PERSISTENT);

	/**
	 * CamelFolder:mark-seen-timeout
	 *
	 * Timeout in milliseconds for marking messages as seen.
	 *
	 * Since: 3.32
	 **/
	properties[PROP_MARK_SEEN_TIMEOUT] =
		g_param_spec_int (
			"mark-seen-timeout", NULL, NULL,
			0, G_MAXINT32,
			1500,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS |
			CAMEL_FOLDER_PARAM_PERSISTENT);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * CamelFolder::changed
	 * @folder: the #CamelFolder which emitted the signal
	 * @changes: the #CamelFolderChangeInfo with the list of changes
	 **/
	signals[CHANGED] = g_signal_new (
		"changed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelFolderClass, changed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		CAMEL_TYPE_FOLDER_CHANGE_INFO);

	/**
	 * CamelFolder::deleted
	 * @folder: the #CamelFolder which emitted the signal
	 **/
	signals[DELETED] = g_signal_new (
		"deleted",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelFolderClass, deleted),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0);

	/**
	 * CamelFolder::renamed
	 * @folder: the #CamelFolder which emitted the signal
	 * @old_name: the previous folder name
	 **/
	signals[RENAMED] = g_signal_new (
		"renamed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelFolderClass, renamed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_STRING);

	camel_folder_class_map_legacy_property(class, "mark-seen", 5);
	camel_folder_class_map_legacy_property(class, "mark-seen-timeout", 6);
}

static void
camel_folder_init (CamelFolder *folder)
{
	folder->priv = camel_folder_get_instance_private (folder);
	folder->priv->frozen = 0;
	folder->priv->changed_frozen = camel_folder_change_info_new ();
	folder->priv->store_changes_after_frozen = FALSE;

	g_rec_mutex_init (&folder->priv->lock);
	g_mutex_init (&folder->priv->change_lock);
	g_mutex_init (&folder->priv->property_lock);
	g_mutex_init (&folder->priv->store_changes_lock);
}

G_DEFINE_QUARK (camel-folder-error-quark, camel_folder_error)

/**
 * camel_folder_set_lock_async:
 * @folder: a #CamelFolder
 * @skip_folder_lock: a value to set
 *
 * Sets whether folder locking (camel_folder_lock() and camel_folder_unlock())
 * should be used. When set to %FALSE, the two functions do nothing and simply
 * return.
 *
 * Since: 2.30
 **/
void
camel_folder_set_lock_async (CamelFolder *folder,
                             gboolean skip_folder_lock)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	folder->priv->skip_folder_lock = skip_folder_lock;
}

/**
 * camel_folder_get_filename:
 * @folder: a #CamelFolder
 * @uid: a message UID
 * @error: return location for a #GError, or %NULL
 *
 * Returns: (transfer full): a file name corresponding to a message
 *   with UID @uid. Free the returned string with g_free(), when
 *   no longer needed.
 *
 * Since: 2.26
 **/
gchar *
camel_folder_get_filename (CamelFolder *folder,
                           const gchar *uid,
                           GError **error)
{
	CamelFolderClass *class;
	gchar *filename;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, NULL);
	g_return_val_if_fail (class->get_filename != NULL, NULL);

	filename = class->get_filename (folder, uid, error);
	CAMEL_CHECK_GERROR (folder, get_filename, filename != NULL, error);

	return filename;
}

/**
 * camel_folder_get_full_name:
 * @folder: a #CamelFolder
 *
 * Returns the fully qualified name of the folder.
 *
 * Returns: the fully qualified name of the folder
 **/
const gchar *
camel_folder_get_full_name (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return folder->priv->full_name;
}

/**
 * camel_folder_dup_full_name:
 * @folder: a #CamelFolder
 *
 * Thread-safe variation of camel_folder_get_full_name().
 * Use this function when accessing @folder from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #CamelFolder:full-name
 *
 * Since: 3.8
 **/
gchar *
camel_folder_dup_full_name (CamelFolder *folder)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	g_mutex_lock (&folder->priv->property_lock);

	protected = camel_folder_get_full_name (folder);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&folder->priv->property_lock);

	return duplicate;
}

/**
 * camel_folder_set_full_name:
 * @folder: a #CamelFolder
 * @full_name: a fully qualified name for the folder
 *
 * Sets the fully qualified name of the folder.
 *
 * Since: 2.32
 **/
void
camel_folder_set_full_name (CamelFolder *folder,
                            const gchar *full_name)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	g_mutex_lock (&folder->priv->property_lock);

	if (g_strcmp0 (folder->priv->full_name, full_name) == 0) {
		g_mutex_unlock (&folder->priv->property_lock);
		return;
	}

	g_free (folder->priv->full_name);
	folder->priv->full_name = g_strdup (full_name);

	g_mutex_unlock (&folder->priv->property_lock);

	g_object_notify_by_pspec (G_OBJECT (folder), properties[PROP_FULL_NAME]);
}

/**
 * camel_folder_get_display_name:
 * @folder: a #CamelFolder
 *
 * Returns the display name for the folder.  The fully qualified name
 * can be obtained with camel_folder_get_full_name().
 *
 * Returns: the display name of the folder
 *
 * Since: 3.2
 **/
const gchar *
camel_folder_get_display_name (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return folder->priv->display_name;
}

/**
 * camel_folder_dup_display_name:
 * @folder: a #CamelFolder
 *
 * Thread-safe variation of camel_folder_get_display_name().
 * Use this function when accessing @folder from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #CamelFolder:display-name
 *
 * Since: 3.8
 **/
gchar *
camel_folder_dup_display_name (CamelFolder *folder)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	g_mutex_lock (&folder->priv->property_lock);

	protected = camel_folder_get_display_name (folder);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&folder->priv->property_lock);

	return duplicate;
}

/**
 * camel_folder_set_display_name:
 * @folder: a #CamelFolder
 * @display_name: a display name for the folder
 *
 * Sets the display name for the folder.
 *
 * Since: 3.2
 **/
void
camel_folder_set_display_name (CamelFolder *folder,
                               const gchar *display_name)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	g_mutex_lock (&folder->priv->property_lock);

	if (g_strcmp0 (folder->priv->display_name, display_name) == 0) {
		g_mutex_unlock (&folder->priv->property_lock);
		return;
	}

	g_free (folder->priv->display_name);
	folder->priv->display_name = g_strdup (display_name);

	g_mutex_unlock (&folder->priv->property_lock);

	g_object_notify_by_pspec (G_OBJECT (folder), properties[PROP_DISPLAY_NAME]);
}

/**
 * camel_folder_get_full_display_name:
 * @folder: a #CamelFolder
 *
 * Similar to the camel_folder_get_full_name(), only returning
 * full path to the @folder suitable for the display to a user.
 *
 * Return: (transfer none): full path to the @folder suitable for the display to a user
 *
 * Since: 3.46
 **/
const gchar *
camel_folder_get_full_display_name (CamelFolder *folder)
{
	CamelFolderClass *klass;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	klass = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (klass != NULL, NULL);
	g_return_val_if_fail (klass->get_full_display_name != NULL, NULL);

	return klass->get_full_display_name (folder);
}

/**
 * camel_folder_get_description:
 * @folder: a #CamelFolder
 *
 * Returns a description of the folder suitable for displaying to the user.
 *
 * Returns: a description of the folder
 *
 * Since: 2.32
 **/
const gchar *
camel_folder_get_description (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	/* Default to full-name if there's no custom description. */
	if (folder->priv->description == NULL)
		return camel_folder_get_full_name (folder);

	return folder->priv->description;
}

/**
 * camel_folder_dup_description:
 * @folder: a #CamelFolder
 *
 * Thread-safe variation of camel_folder_get_description().
 * Use this function when accessing @folder from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #CamelFolder:description
 *
 * Since: 3.8
 **/
gchar *
camel_folder_dup_description (CamelFolder *folder)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	g_mutex_lock (&folder->priv->property_lock);

	protected = camel_folder_get_description (folder);
	duplicate = g_strdup (protected);

	g_mutex_unlock (&folder->priv->property_lock);

	return duplicate;
}

/**
 * camel_folder_set_description:
 * @folder: a #CamelFolder
 * @description: a description of the folder
 *
 * Sets a description of the folder suitable for displaying to the user.
 *
 * Since: 2.32
 **/
void
camel_folder_set_description (CamelFolder *folder,
                              const gchar *description)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	g_mutex_lock (&folder->priv->property_lock);

	if (g_strcmp0 (folder->priv->description, description) == 0) {
		g_mutex_unlock (&folder->priv->property_lock);
		return;
	}

	g_free (folder->priv->description);
	folder->priv->description = g_strdup (description);

	g_mutex_unlock (&folder->priv->property_lock);

	g_object_notify_by_pspec (G_OBJECT (folder), properties[PROP_DESCRIPTION]);
}

/**
 * camel_folder_get_parent_store:
 * @folder: a #CamelFolder
 *
 * Returns: (transfer none): the parent #CamelStore of the folder
 **/
CamelStore *
camel_folder_get_parent_store (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	/* Can be NULL, thus do not use CAMEL_STORE() macro. */
	return (CamelStore *) (folder->priv->parent_store);
}

/**
 * camel_folder_get_folder_summary:
 * @folder: a #CamelFolder
 *
 * Get the #CamelFolderSummary if the backend actually supports it.
 * The camel_folder_has_summary_capability() conveniently checks its availability.
 *
 * Returns: (transfer none) (nullable): a #CamelFolderSummary of the folder
 *
 * Since: 3.24
 **/
CamelFolderSummary *
camel_folder_get_folder_summary (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return folder->priv->summary;
}

/**
 * camel_folder_take_folder_summary:
 * @folder: a #CamelFolder
 * @summary: (transfer full): a #CamelFolderSummary
 *
 * Sets a #CamelFolderSummary of the folder. It consumes the @summary.
 *
 * This is supposed to be called only by the descendants of
 * the #CamelFolder and only at the construction time. Calling
 * this function twice yields to an error.
 *
 * Since: 3.24
 **/
void
camel_folder_take_folder_summary (CamelFolder *folder,
				  CamelFolderSummary *summary)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary));
	g_return_if_fail (folder->priv->summary == NULL);

	folder->priv->summary = summary;
}

/**
 * camel_folder_get_message_count:
 * @folder: a #CamelFolder
 *
 * Returns: the number of messages in the folder, or -1 if unknown
 **/
gint
camel_folder_get_message_count (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, -1);
	g_return_val_if_fail (class->get_message_count != NULL, -1);

	return class->get_message_count (folder);
}

/**
 * camel_folder_get_flags:
 * @folder: a #CamelFolder
 *
 * Returns: Folder flags (bit-or of #CamelFolderFlags) of the @folder
 *
 * Since: 3.24
 **/
guint32
camel_folder_get_flags (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	return folder->priv->folder_flags;
}

/**
 * camel_folder_set_flags:
 * @folder: a #CamelFolder
 * @folder_flags: flags (bit-or of #CamelFolderFlags) to set
 *
 * Sets folder flags (bit-or of #CamelFolderFlags) for the @folder.
 *
 * Since: 3.24
 **/
void
camel_folder_set_flags (CamelFolder *folder,
			guint32 folder_flags)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	folder->priv->folder_flags = folder_flags;
}

/**
 * camel_folder_get_mark_seen:
 * @folder: a #CamelFolder
 *
 * Returns: a #CamelThreeState, whether messages in this @folder
 *    should be marked as seen automatically.
 *
 * Since: 3.32
 **/
CamelThreeState
camel_folder_get_mark_seen (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), CAMEL_THREE_STATE_INCONSISTENT);

	return folder->priv->mark_seen;
}

/**
 * camel_folder_set_mark_seen:
 * @folder: a #CamelFolder
 * @mark_seen: a #CamelThreeState as the value to set
 *
 * Sets whether the messages in this @folder should be marked
 * as seen automatically. An inconsistent state means to use
 * global option.
 *
 * Since: 3.32
 **/
void
camel_folder_set_mark_seen (CamelFolder *folder,
			    CamelThreeState mark_seen)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	if (folder->priv->mark_seen == mark_seen)
		return;

	folder->priv->mark_seen = mark_seen;

	g_object_notify_by_pspec (G_OBJECT (folder), properties[PROP_MARK_SEEN]);
}

/**
 * camel_folder_get_mark_seen_timeout:
 * @folder: a #CamelFolder
 *
 * Returns: timeout in milliseconds for marking messages
 *    as seen in this @folder
 *
 * Since: 3.32
 **/
gint
camel_folder_get_mark_seen_timeout (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	return folder->priv->mark_seen_timeout;
}

/**
 * camel_folder_set_mark_seen_timeout:
 * @folder: a #CamelFolder
 * @timeout: a timeout in milliseconds
 *
 * Sets the @timeout in milliseconds for marking messages
 * as seen in this @folder. Whether the timeout is used
 * depends on camel_folder_get_mark_seen().
 *
 * Since: 3.32
 **/
void
camel_folder_set_mark_seen_timeout (CamelFolder *folder,
				    gint timeout)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	if (folder->priv->mark_seen_timeout == timeout)
		return;

	folder->priv->mark_seen_timeout = timeout;

	g_object_notify_by_pspec (G_OBJECT (folder), properties[PROP_MARK_SEEN_TIMEOUT]);
}

/**
 * camel_folder_get_permanent_flags:
 * @folder: a #CamelFolder
 *
 * Returns: the set of #CamelMessageFlags that can be permanently
 * stored on a message between sessions. If it includes
 * #CAMEL_MESSAGE_USER, then user-defined flags will be remembered.
 **/
guint32
camel_folder_get_permanent_flags (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, 0);
	g_return_val_if_fail (class->get_permanent_flags != NULL, 0);

	return class->get_permanent_flags (folder);
}

/**
 * camel_folder_get_message_flags:
 * @folder: a #CamelFolder
 * @uid: the UID of a message in @folder
 *
 * Returns: the #CamelMessageFlags that are set on the indicated
 * message.
 **/
guint32
camel_folder_get_message_flags (CamelFolder *folder,
                                const gchar *uid)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);
	g_return_val_if_fail (uid != NULL, 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, 0);
	g_return_val_if_fail (class->get_message_flags != NULL, 0);

	return class->get_message_flags (folder, uid);
}

/**
 * camel_folder_set_message_flags:
 * @folder: a #CamelFolder
 * @uid: the UID of a message in @folder
 * @mask: a mask of #CamelMessageFlags bit-or values to use
 * @set: the flags to set, also bit-or of #CamelMessageFlags
 *
 * Sets those flags specified by @mask to the values specified by @set
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See camel_folder_get_permanent_flags())
 *
 * E.g. to set the deleted flag and clear the draft flag, use
 * camel_folder_set_message_flags (folder, uid, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_DRAFT, CAMEL_MESSAGE_DELETED);
 *
 * Returns: %TRUE if the flags were changed or %FALSE otherwise
 **/
gboolean
camel_folder_set_message_flags (CamelFolder *folder,
                                const gchar *uid,
                                guint32 mask,
                                guint32 set)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->set_message_flags != NULL, FALSE);

	return class->set_message_flags (folder, uid, mask, set);
}

/**
 * camel_folder_get_message_info:
 * @folder: a #CamelFolder
 * @uid: the uid of a message
 *
 * Retrieve the #CamelMessageInfo for the specified @uid.
 *
 * Returns: (transfer full) (nullable): The summary information for the
 *   indicated message, or %NULL if the uid does not exist. Free the returned
 *   object with g_object_unref(), when done with it.
 **/
CamelMessageInfo *
camel_folder_get_message_info (CamelFolder *folder,
                               const gchar *uid)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, NULL);
	g_return_val_if_fail (class->get_message_info != NULL, NULL);

	return class->get_message_info (folder, uid);
}

/* TODO: is this function required anyway? */
/**
 * camel_folder_has_summary_capability:
 * @folder: a #CamelFolder
 *
 * Get whether or not the folder has a summary.
 *
 * Returns: %TRUE if a summary is available or %FALSE otherwise
 **/
gboolean
camel_folder_has_summary_capability (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	return (camel_folder_get_flags (folder) & CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY) != 0;
}

/* UIDs stuff */

/**
 * camel_folder_dup_uids:
 * @folder: a #CamelFolder
 *
 * Duplicates a list of UIDs available in the @folder. Free the array
 * with g_ptr_array_unref(), when no longer needed.
 *
 * Returns: (element-type utf8) (transfer container): a new #GPtrArray
 *    of UIDs corresponding to the messages available in the @folder
 *
 * Since: 3.58
 **/
GPtrArray *
camel_folder_dup_uids (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, NULL);
	g_return_val_if_fail (class->dup_uids != NULL, NULL);

	return class->dup_uids (folder);
}

/**
 * camel_folder_dup_uncached_uids:
 * @folder: a #CamelFolder
 * @uids: (element-type utf8): the array of uids to filter down to uncached ones.
 * @error: return location for a #GError, or %NULL
 *
 * Returns the known-uncached uids from a list of uids. It may return uids
 * which are locally cached but should never filter out a uid which is not
 * locally cached.
 *
 * Free the result with g_ptr_array_unref(), when no longer needed.
 *
 * Returns: (element-type utf8) (transfer container): a new #GPtrArray with UID-s,
 *    which are not cached locally
 *
 * Since: 3.58
 **/
GPtrArray *
camel_folder_dup_uncached_uids (CamelFolder *folder,
                                GPtrArray *uids,
                                GError **error)
{
	CamelFolderClass *class;
	GPtrArray *uncached_uids;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uids != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, NULL);
	g_return_val_if_fail (class->dup_uncached_uids != NULL, NULL);

	uncached_uids = class->dup_uncached_uids (folder, uids, error);
	CAMEL_CHECK_GERROR (folder, dup_uncached_uids, uncached_uids != NULL, error);

	return uncached_uids;
}

/**
 * camel_folder_cmp_uids:
 * @folder: a #CamelFolder
 * @uid1: The first uid.
 * @uid2: the second uid.
 *
 * Compares two uids. The return value meaning is the same as in any other compare function.
 *
 * Note that the default compare function expects a decimal number at the beginning of a uid,
 * thus if provider uses different uid values, then it should subclass this function.
 *
 * Since: 2.28
 **/
gint
camel_folder_cmp_uids (CamelFolder *folder,
                       const gchar *uid1,
                       const gchar *uid2)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);
	g_return_val_if_fail (uid1 != NULL, 0);
	g_return_val_if_fail (uid2 != NULL, 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, 0);
	g_return_val_if_fail (class->cmp_uids != NULL, 0);

	return class->cmp_uids (folder, uid1, uid2);
}

/**
 * camel_folder_sort_uids:
 * @folder: a #CamelFolder
 * @uids: (element-type utf8): array of uids
 *
 * Sorts the array of UIDs.
 *
 * Since: 2.24
 **/
void
camel_folder_sort_uids (CamelFolder *folder,
                        GPtrArray *uids)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uids != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->sort_uids != NULL);

	class->sort_uids (folder, uids);
}

/**
 * camel_folder_search_sync:
 * @folder: a #CamelFolder
 * @expression: a search expression
 * @out_uids: (out) (element-type utf8) (transfer container) (nullable): return location
 *    for a #GPtrArray of uids of matching messages, or %NULL when none found or error happened
 * @cancellable: a #GCancellable
 * @error: return location for a #GError, or %NULL
 *
 * Searches the folder for messages matching the given search expression.
 *
 * Free the array with g_ptr_array_unref(), when no longer needed.
 *
 * Returns: whether succeeded
 *
 * Since: 3.58
 **/
gboolean
camel_folder_search_sync (CamelFolder *folder,
			  const gchar *expression,
			  GPtrArray **out_uids,
			  GCancellable *cancellable,
			  GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->search_sync != NULL, FALSE);

	return class->search_sync (folder, expression, out_uids, cancellable, error);
}

/**
 * camel_folder_delete:
 * @folder: a #CamelFolder
 *
 * Marks @folder as deleted and performs any required cleanup.
 *
 * This also emits the #CamelFolder::deleted signal from an idle source on
 * the main loop.  The idle source's priority is #G_PRIORITY_HIGH_IDLE.
 **/
void
camel_folder_delete (CamelFolder *folder)
{
	CamelFolderClass *class;
	CamelStore *parent_store;
	CamelService *service;
	CamelSession *session;
	SignalClosure *signal_closure;
	const gchar *full_name;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->delete_ != NULL);

	camel_folder_lock (folder);
	if (camel_folder_get_flags (folder) & CAMEL_FOLDER_HAS_BEEN_DELETED) {
		camel_folder_unlock (folder);
		return;
	}

	camel_folder_set_flags (folder, camel_folder_get_flags (folder) | CAMEL_FOLDER_HAS_BEEN_DELETED);

	class->delete_ (folder);

	camel_folder_unlock (folder);

	/* Delete the references of the folder from the DB.*/
	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	camel_store_db_delete_folder (camel_store_get_db (parent_store), full_name, NULL);

	service = CAMEL_SERVICE (parent_store);
	session = camel_service_ref_session (service);
	if (!session)
		return;

	signal_closure = g_slice_new0 (SignalClosure);
	signal_closure->folder = g_object_ref (folder);

	/* Prioritize ahead of GTK+ redraws. */
	camel_session_idle_add (
		session, G_PRIORITY_HIGH_IDLE,
		folder_emit_deleted_cb,
		signal_closure,
		(GDestroyNotify) signal_closure_free);

	g_object_unref (session);
}

/**
 * camel_folder_rename:
 * @folder: a #CamelFolder
 * @new_name: new name for the folder
 *
 * Marks @folder as renamed.
 *
 * This also emits the #CamelFolder::renamed signal from an idle source on
 * the main loop.  The idle source's priority is #G_PRIORITY_HIGH_IDLE.
 *
 * NOTE: This is an internal function used by camel stores, no locking
 * is performed on the folder.
 **/
void
camel_folder_rename (CamelFolder *folder,
                     const gchar *new_name)
{
	CamelFolderClass *class;
	CamelStore *parent_store;
	CamelService *service;
	CamelSession *session;
	SignalClosure *signal_closure;
	gchar *old_name;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (new_name != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->rename != NULL);

	old_name = g_strdup (camel_folder_get_full_name (folder));

	class->rename (folder, new_name);

	parent_store = camel_folder_get_parent_store (folder);
	camel_store_db_rename_folder (camel_store_get_db (parent_store), old_name, new_name, NULL);

	service = CAMEL_SERVICE (parent_store);
	session = camel_service_ref_session (service);
	if (!session) {
		g_free (old_name);
		return;
	}

	signal_closure = g_slice_new0 (SignalClosure);
	signal_closure->folder = g_object_ref (folder);
	signal_closure->folder_name = old_name;  /* transfer ownership */

	/* Prioritize ahead of GTK+ redraws. */
	camel_session_idle_add (
		session, G_PRIORITY_HIGH_IDLE,
		folder_emit_renamed_cb,
		signal_closure,
		(GDestroyNotify) signal_closure_free);

	g_object_unref (session);
}

/**
 * camel_folder_changed:
 * @folder: a #CamelFolder
 * @changes: change information for @folder
 *
 * Emits the #CamelFolder::changed signal from an idle source on the
 * main loop.  The idle source's priority is #G_PRIORITY_LOW.
 *
 * Since: 2.32
 **/
void
camel_folder_changed (CamelFolder *folder,
                      CamelFolderChangeInfo *changes)
{
	CamelFolderChangeInfo *pending_changes;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (changes != NULL);

	if (camel_folder_is_frozen (folder)) {
		/* folder_changed() will catch this case and pile
		 * the changes into folder->changed_frozen */
		g_signal_emit (folder, signals[CHANGED], 0, changes);
		return;
	}

	/* If a "changed" signal has already been scheduled but not yet
	 * emitted, just append our changes to the pending changes, and
	 * skip scheduling our own "changed" signal.  This helps to cut
	 * down on the frequency of signal emissions so virtual folders
	 * won't have to work so hard. */

	g_mutex_lock (&folder->priv->change_lock);

	pending_changes = folder->priv->pending_changes;

	if (pending_changes == NULL) {
		CamelStore *parent_store;
		CamelService *service;
		CamelSession *session;
		SignalClosure *signal_closure;

		parent_store = camel_folder_get_parent_store (folder);
		if (parent_store) {
			service = CAMEL_SERVICE (parent_store);
			session = camel_service_ref_session (service);

			if (session) {
				pending_changes = camel_folder_change_info_new ();
				folder->priv->pending_changes = pending_changes;

				signal_closure = g_slice_new0 (SignalClosure);
				signal_closure->folder = g_object_ref (folder);

				camel_session_idle_add (
					session, G_PRIORITY_LOW,
					folder_emit_changed_cb,
					signal_closure,
					(GDestroyNotify) signal_closure_free);

				g_object_unref (session);
			}
		}
	}

	camel_folder_change_info_cat (pending_changes, changes);

	g_mutex_unlock (&folder->priv->change_lock);
}

/**
 * camel_folder_freeze:
 * @folder: a #CamelFolder
 *
 * Freezes the folder so that a series of operation can be performed
 * without "folder_changed" signals being emitted.  When the folder is
 * later thawed with camel_folder_thaw(), the suppressed signals will
 * be emitted.
 **/
void
camel_folder_freeze (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->freeze != NULL);

	class->freeze (folder);
}

/**
 * camel_folder_thaw:
 * @folder: a #CamelFolder
 *
 * Thaws the folder and emits any pending folder_changed
 * signals.
 **/
void
camel_folder_thaw (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (folder->priv->frozen != 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->thaw != NULL);

	class->thaw (folder);
}

/**
 * camel_folder_is_frozen:
 * @folder: a #CamelFolder
 *
 * Returns: whether or not the folder is frozen
 **/
gboolean
camel_folder_is_frozen (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->is_frozen != NULL, FALSE);

	return class->is_frozen (folder);
}

/**
 * camel_folder_get_frozen_count:
 * @folder: a #CamelFolder
 *
 * Since: 2.32
 **/
gint
camel_folder_get_frozen_count (CamelFolder *folder)
{
	/* FIXME This function shouldn't be needed,
	 *       but it's used in CamelVeeFolder */
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	return folder->priv->frozen;
}

/**
 * camel_folder_quota_info_new:
 * @name: Name of the quota.
 * @used: Current usage of the quota.
 * @total: Total available size of the quota.
 *
 * Returns: newly allocated #CamelFolderQuotaInfo structure with
 * initialized values based on the parameters, with next member set to NULL.
 *
 * Since: 2.24
 **/
CamelFolderQuotaInfo *
camel_folder_quota_info_new (const gchar *name,
                             guint64 used,
                             guint64 total)
{
	CamelFolderQuotaInfo *info;

	info = g_malloc0 (sizeof (CamelFolderQuotaInfo));
	info->name = g_strdup (name);
	info->used = used;
	info->total = total;
	info->next = NULL;

	return info;
}

/**
 * camel_folder_quota_info_clone:
 * @info: a #CamelFolderQuotaInfo object to clone.
 *
 * Makes a copy of the given info and all next-s.
 *
 * Since: 2.24
 **/
CamelFolderQuotaInfo *
camel_folder_quota_info_clone (const CamelFolderQuotaInfo *info)
{
	CamelFolderQuotaInfo *clone = NULL, *last = NULL;
	const CamelFolderQuotaInfo *iter;

	for (iter = info; iter != NULL; iter = iter->next) {
		CamelFolderQuotaInfo *n = camel_folder_quota_info_new (iter->name, iter->used, iter->total);

		if (last)
			last->next = n;
		else
			clone = n;

		last = n;
	}

	return clone;
}

/**
 * camel_folder_quota_info_free:
 * @info: a #CamelFolderQuotaInfo object to free.
 *
 * Frees this and all next objects.
 *
 * Since: 2.24
 **/
void
camel_folder_quota_info_free (CamelFolderQuotaInfo *info)
{
	CamelFolderQuotaInfo *next = info;

	while (next) {
		info = next;
		next = next->next;

		g_free (info->name);
		g_free (info);
	}
}

/**
 * camel_folder_lock:
 * @folder: a #CamelFolder
 *
 * Locks @folder. Unlock it with camel_folder_unlock().
 *
 * Since: 2.32
 **/
void
camel_folder_lock (CamelFolder *folder)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	if (folder->priv->skip_folder_lock == FALSE)
		g_rec_mutex_lock (&folder->priv->lock);
}

/**
 * camel_folder_unlock:
 * @folder: a #CamelFolder
 *
 * Unlocks @folder, previously locked with camel_folder_lock().
 *
 * Since: 2.32
 **/
void
camel_folder_unlock (CamelFolder *folder)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	if (folder->priv->skip_folder_lock == FALSE)
		g_rec_mutex_unlock (&folder->priv->lock);
}

/**
 * camel_folder_append_message_sync:
 * @folder: a #CamelFolder
 * @message: a #CamelMimeMessage
 * @info: (nullable): a #CamelMessageInfo with additional flags/etc to set
 *        on the new message, or %NULL
 * @appended_uid: (out) (optional) (nullable): if non-%NULL, the UID
 *                of the appended message will be returned here, if it
 *                is known
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Appends @message to @folder.  Only the flag and tag data from @info
 * are used.  If @info is %NULL, no flags or tags will be set.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_append_message_sync (CamelFolder *folder,
                                  CamelMimeMessage *message,
                                  CamelMessageInfo *info,
                                  gchar **appended_uid,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->append_message_sync != NULL, FALSE);

	/* Need to connect the service before we can append. */
	success = folder_maybe_connect_sync (folder, cancellable, error);
	if (!success)
		return FALSE;

	camel_folder_lock (folder);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder);
		return FALSE;
	}

	success = class->append_message_sync (
		folder, message, info, appended_uid, cancellable, error);
	CAMEL_CHECK_GERROR (folder, append_message_sync, success, error);

	camel_folder_unlock (folder);

	return success;
}

/* Helper for camel_folder_append_message() */
static void
folder_append_message_thread (GTask *task,
                              gpointer source_object,
                              gpointer task_data,
                              GCancellable *cancellable)
{
	gboolean success;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	success = camel_folder_append_message_sync (
		CAMEL_FOLDER (source_object),
		async_context->message,
		async_context->info,
		&async_context->message_uid,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * camel_folder_append_message:
 * @folder: a #CamelFolder
 * @message: a #CamelMimeMessage
 * @info: (nullable): a #CamelMessageInfo with additional flags/etc to set
 *        on the new message, or %NULL
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Appends @message to @folder asynchronously.  Only the flag and tag data
 * from @info are used.  If @info is %NULL, no flags or tags will be set.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_folder_append_message_finish() to get the result of
 * the operation.
 *
 * Since: 3.0
 **/
void
camel_folder_append_message (CamelFolder *folder,
                             CamelMimeMessage *message,
                             CamelMessageInfo *info,
                             gint io_priority,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));

	async_context = g_slice_new0 (AsyncContext);
	async_context->message = g_object_ref (message);
	async_context->info = g_object_ref (info);

	task = g_task_new (folder, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_folder_append_message);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, folder_append_message_thread);

	g_object_unref (task);
}

/**
 * camel_folder_append_message_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @appended_uid: (out) (optional) (nullable): if non-%NULL, the UID of
 *                the appended message will be returned here, if it is
 *                known
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_folder_append_message_finish().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_append_message_finish (CamelFolder *folder,
                                    GAsyncResult *result,
                                    gchar **appended_uid,
                                    GError **error)
{
	AsyncContext *async_context;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, folder), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_folder_append_message), FALSE);

	async_context = g_task_get_task_data (G_TASK (result));

	if (!g_task_had_error (G_TASK (result))) {
		if (appended_uid != NULL) {
			*appended_uid = async_context->message_uid;
			async_context->message_uid = NULL;
		}
	}

	return g_task_propagate_boolean (G_TASK (result), error);
}

static gboolean
camel_folder_maybe_run_db_maintenance (CamelFolder *folder,
				       GError **error)
{
	return camel_store_maybe_run_db_maintenance (camel_folder_get_parent_store (folder), error);
}

/**
 * camel_folder_expunge_sync:
 * @folder: a #CamelFolder
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes messages which have been marked as "DELETED".
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_expunge_sync (CamelFolder *folder,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->expunge_sync != NULL, FALSE);

	/* Need to connect the service before we can expunge. */
	success = folder_maybe_connect_sync (folder, cancellable, error);
	if (!success)
		return FALSE;

	camel_folder_lock (folder);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder);
		return FALSE;
	}

	/* Translators: The first “%s” is replaced with an account name and the second “%s”
	   is replaced with a full path name. The spaces around “:” are intentional, as
	   the whole “%s : %s” is meant as an absolute identification of the folder. */
	camel_operation_push_message (cancellable, _("Expunging folder “%s : %s”"),
		camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))),
		camel_folder_get_full_display_name (folder));

	if (!(camel_folder_get_flags (folder) & CAMEL_FOLDER_HAS_BEEN_DELETED)) {
		success = class->expunge_sync (folder, cancellable, error);
		CAMEL_CHECK_GERROR (folder, expunge_sync, success, error);

		if (success)
			success = camel_folder_maybe_run_db_maintenance (folder, error);
	}

	camel_operation_pop_message (cancellable);

	camel_folder_unlock (folder);

	return success;
}

/* Helper for camel_folder_expunge() */
static void
folder_expunge_thread (GTask *task,
                       gpointer source_object,
                       gpointer task_data,
                       GCancellable *cancellable)
{
	gboolean success;
	GError *local_error = NULL;

	success = camel_folder_expunge_sync (
		CAMEL_FOLDER (source_object),
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * camel_folder_expunge:
 * @folder: a #CamelFolder
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously deletes messages which have been marked as "DELETED".
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_folder_expunge_finish() to get the result of the operation.
 *
 * Since: 3.0
 **/
void
camel_folder_expunge (CamelFolder *folder,
                      gint io_priority,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	GTask *task;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	task = g_task_new (folder, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_folder_expunge);
	g_task_set_priority (task, io_priority);

	g_task_run_in_thread (task, folder_expunge_thread);

	g_object_unref (task);
}

/**
 * camel_folder_expunge_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_folder_expunge().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_expunge_finish (CamelFolder *folder,
                             GAsyncResult *result,
                             GError **error)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, folder), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_folder_expunge), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * camel_folder_get_message_sync:
 * @folder: a #CamelFolder
 * @message_uid: the message UID
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets the message corresponding to @message_uid from @folder.
 *
 * Returns: (transfer none): a #CamelMimeMessage corresponding to the requested UID
 *
 * Since: 3.0
 **/
CamelMimeMessage *
camel_folder_get_message_sync (CamelFolder *folder,
                               const gchar *message_uid,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelFolderClass *class;
	CamelMimeMessage *message;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (message_uid != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, NULL);
	g_return_val_if_fail (class->get_message_sync != NULL, NULL);

	camel_operation_push_message (
		/* Translators: The first “%s” is replaced with an account name and the second “%s”
		   is replaced with a full path name. The spaces around “:” are intentional, as
		   the whole “%s : %s” is meant as an absolute identification of the folder. */
		cancellable, _("Retrieving message “%s” in “%s : %s”"),
		message_uid, camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))),
		camel_folder_get_full_display_name (folder));

	message = camel_folder_get_message_cached (folder, message_uid, cancellable);

	if (message == NULL) {
		/* Recover from a dropped connection, unless we're offline. */
		if (!folder_maybe_connect_sync (folder, cancellable, error)) {
			camel_operation_pop_message (cancellable);
			return NULL;
		}

		camel_folder_lock (folder);

		/* Check for cancellation after locking. */
		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			camel_folder_unlock (folder);
			camel_operation_pop_message (cancellable);
			return NULL;
		}

		message = class->get_message_sync (
			folder, message_uid, cancellable, error);
		CAMEL_CHECK_GERROR (
			folder, get_message_sync, message != NULL, error);

		camel_folder_unlock (folder);
	}

	if (message && camel_mime_message_get_source (message) == NULL) {
		CamelStore *store;
		const gchar *uid;

		store = camel_folder_get_parent_store (folder);
		uid = camel_service_get_uid (CAMEL_SERVICE (store));

		camel_mime_message_set_source (message, uid);
	}

	camel_operation_pop_message (cancellable);

	if (message != NULL && camel_debug_start (":folder")) {
		printf (
			"CamelFolder:get_message ('%s', '%s') =\n",
			camel_folder_get_full_name (folder), message_uid);
		camel_mime_message_dump (message, FALSE);
		camel_debug_end ();
	}

	return message;
}

/**
 * camel_folder_get_message_cached:
 * @folder: a #CamelFolder
 * @message_uid: the message UID
 * @cancellable: optional #GCancellable object, or %NULL
 *
 * Gets the message corresponding to @message_uid from the @folder cache,
 * if available locally. This should not do any network I/O, only check
 * if message is already downloaded and return it quickly, not being
 * blocked by the folder's lock. Returning NULL is not considered as
 * an error, it just means that the message is still to-be-downloaded.
 *
 * Note: This function is called automatically within camel_folder_get_message_sync().
 *
 * Returns: (transfer full) (nullable): a cached #CamelMimeMessage corresponding
 *    to the requested UID
 *
 * Since: 3.24
 **/
CamelMimeMessage *
camel_folder_get_message_cached (CamelFolder *folder,
				 const gchar *message_uid,
				 GCancellable *cancellable)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (message_uid != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, NULL);

	if (!class->get_message_cached)
		return NULL;

	return class->get_message_cached (folder, message_uid, cancellable);
}

/* Helper for camel_folder_get_message() */
static void
folder_get_message_thread (GTask *task,
                           gpointer source_object,
                           gpointer task_data,
                           GCancellable *cancellable)
{
	CamelMimeMessage *message;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	message = camel_folder_get_message_sync (
		CAMEL_FOLDER (source_object),
		async_context->message_uid,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_warn_if_fail (message == NULL);
		g_task_return_error (task, local_error);
	} else {
		g_task_return_pointer (
			task, message,
			(GDestroyNotify) g_object_unref);
	}
}

/**
 * camel_folder_get_message:
 * @folder: a #CamelFolder
 * @message_uid: the message UID
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously gets the message corresponding to @message_uid from @folder.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_folder_get_message_finish() to get the result of the operation.
 *
 * Since: 3.0
 **/
void
camel_folder_get_message (CamelFolder *folder,
                          const gchar *message_uid,
                          gint io_priority,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (message_uid != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->message_uid = g_strdup (message_uid);

	task = g_task_new (folder, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_folder_get_message);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, folder_get_message_thread);

	g_object_unref (task);
}

/**
 * camel_folder_get_message_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError or %NULL
 *
 * Finishes the operation started with camel_folder_get_message().
 *
 * Returns: (transfer none): a #CamelMimeMessage corresponding to the requested UID
 *
 * Since: 3.0
 **/
CamelMimeMessage *
camel_folder_get_message_finish (CamelFolder *folder,
                                 GAsyncResult *result,
                                 GError **error)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (g_task_is_valid (result, folder), NULL);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_folder_get_message), NULL);

	return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * camel_folder_get_quota_info_sync:
 * @folder: a #CamelFolder
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets a list of known quotas for @folder.  Free the returned
 * #CamelFolderQuotaInfo struct with camel_folder_quota_info_free().
 *
 * If quotas are not supported for @folder, the function returns %NULL
 * and sets @error to #G_IO_ERROR_NOT_SUPPORTED.
 *
 * Returns: a #CamelFolderQuotaInfo, or %NULL on error
 *
 * Since: 3.2
 **/
CamelFolderQuotaInfo *
camel_folder_get_quota_info_sync (CamelFolder *folder,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelFolderClass *class;
	CamelFolderQuotaInfo *quota_info;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, NULL);
	g_return_val_if_fail (class->get_quota_info_sync != NULL, NULL);

	/* Translators: The first “%s” is replaced with an account name and the second “%s”
	   is replaced with a full path name. The spaces around “:” are intentional, as
	   the whole “%s : %s” is meant as an absolute identification of the folder. */
	camel_operation_push_message (cancellable, _("Retrieving quota information for “%s : %s”"),
		camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))),
		camel_folder_get_full_display_name (folder));

	quota_info = class->get_quota_info_sync (folder, cancellable, error);
	CAMEL_CHECK_GERROR (
		folder, get_quota_info_sync, quota_info != NULL, error);

	camel_operation_pop_message (cancellable);

	return quota_info;
}

/* Helper for camel_folder_get_quota_info() */
static void
folder_get_quota_info_thread (GTask *task,
                              gpointer source_object,
                              gpointer task_data,
                              GCancellable *cancellable)
{
	CamelFolderQuotaInfo *quota_info;
	GError *local_error = NULL;

	quota_info = camel_folder_get_quota_info_sync (
		CAMEL_FOLDER (source_object),
		cancellable, &local_error);

	if (local_error != NULL) {
		g_warn_if_fail (quota_info == NULL);
		g_task_return_error (task, local_error);
	} else {
		g_task_return_pointer (
			task, quota_info,
			(GDestroyNotify) camel_folder_quota_info_free);
	}
}

/**
 * camel_folder_get_quota_info:
 * @folder: a #CamelFolder
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously gets a list of known quotas for @folder.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_folder_get_quota_info_finish() to get the result of
 * the operation.
 *
 * Since: 3.2
 **/
void
camel_folder_get_quota_info (CamelFolder *folder,
                             gint io_priority,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GTask *task;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	task = g_task_new (folder, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_folder_get_quota_info);
	g_task_set_priority (task, io_priority);

	g_task_run_in_thread (task, folder_get_quota_info_thread);

	g_object_unref (task);
}

/**
 * camel_folder_get_quota_info_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError or %NULL
 *
 * Finishes the operation started with camel_folder_get_quota_info().
 * Free the returned #CamelFolderQuotaInfo struct with
 * camel_folder_quota_info_free().
 *
 * If quotas are not supported for @folder, the function returns %NULL
 * and sets @error to #G_IO_ERROR_NOT_SUPPORTED.
 *
 * Returns: a #CamelFolderQuotaInfo, or %NULL on error
 *
 * Since: 3.2
 **/
CamelFolderQuotaInfo *
camel_folder_get_quota_info_finish (CamelFolder *folder,
                                    GAsyncResult *result,
                                    GError **error)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (g_task_is_valid (result, folder), NULL);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_folder_get_quota_info), NULL);

	return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * camel_folder_purge_message_cache_sync:
 * @folder: a #CamelFolder
 * @start_uid: the start message UID
 * @end_uid: the end message UID
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Delete the local cache of all messages between these uids.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.4
 **/
gboolean
camel_folder_purge_message_cache_sync (CamelFolder *folder,
                                       gchar *start_uid,
                                       gchar *end_uid,
                                       GCancellable *cancellable,
                                       GError **error)
{
	CamelFolderClass *class;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, FALSE);

	/* Some backends that wont support mobile
	 * mode, won't have this api implemented. */
	if (class->purge_message_cache_sync == NULL)
		return FALSE;

	camel_folder_lock (folder);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder);
		return FALSE;
	}

	success = class->purge_message_cache_sync (
		folder, start_uid, end_uid, cancellable, error);
	CAMEL_CHECK_GERROR (folder, purge_message_cache_sync, success, error);

	camel_folder_unlock (folder);

	return success;
}

/* Helper for camel_purge_message_cache() */
static void
folder_purge_message_cache_thread (GTask *task,
                                   gpointer source_object,
                                   gpointer task_data,
                                   GCancellable *cancellable)
{
	gboolean success;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	success = camel_folder_purge_message_cache_sync (
		CAMEL_FOLDER (source_object),
		async_context->start_uid,
		async_context->end_uid,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * camel_folder_purge_message_cache:
 * @folder: a #CamelFolder
 * @start_uid: the start message UID
 * @end_uid: the end message UID 
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Delete the local cache of all messages between these uids.
 * 
 * When the operation is finished, @callback will be called.  You can then
 * call camel_folder_purge_message_cache_finish() to get the result of the
 * operation.
 *
 * Since: 3.4
 **/
void
camel_folder_purge_message_cache (CamelFolder *folder,
                                  gchar *start_uid,
                                  gchar *end_uid,
                                  gint io_priority,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	async_context = g_slice_new0 (AsyncContext);
	async_context->start_uid = g_strdup (start_uid);
	async_context->end_uid = g_strdup (end_uid);

	task = g_task_new (folder, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_folder_purge_message_cache);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, folder_purge_message_cache_thread);

	g_object_unref (task);
}

/**
 * camel_folder_purge_message_cache_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_folder_purge_message_cache().
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.4
 **/
gboolean
camel_folder_purge_message_cache_finish (CamelFolder *folder,
                                         GAsyncResult *result,
                                         GError **error)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, folder), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_folder_purge_message_cache), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * camel_folder_refresh_info_sync:
 * @folder: a #CamelFolder
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Synchronizes a folder's summary with its backing store.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_refresh_info_sync (CamelFolder *folder,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->refresh_info_sync != NULL, FALSE);

	/* Need to connect the service before we can refresh. */
	success = folder_maybe_connect_sync (folder, cancellable, error);
	if (!success)
		return FALSE;

	camel_folder_lock (folder);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder);
		return FALSE;
	}

	/* Translators: The first “%s” is replaced with an account name and the second “%s”
	   is replaced with a full path name. The spaces around “:” are intentional, as
	   the whole “%s : %s” is meant as an absolute identification of the folder. */
	camel_operation_push_message (cancellable, _("Refreshing folder “%s : %s”"),
		camel_service_get_display_name (CAMEL_SERVICE (camel_folder_get_parent_store (folder))),
		camel_folder_get_full_display_name (folder));

	success = class->refresh_info_sync (folder, cancellable, error);
	CAMEL_CHECK_GERROR (folder, refresh_info_sync, success, error);

	camel_operation_pop_message (cancellable);

	camel_folder_unlock (folder);

	return success;
}

/* Helper for camel_folder_refresh_info() */
static void
folder_refresh_info_thread (GTask *task,
                            gpointer source_object,
                            gpointer task_data,
                            GCancellable *cancellable)
{
	gboolean success;
	GError *local_error = NULL;

	success = camel_folder_refresh_info_sync (
		CAMEL_FOLDER (source_object),
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * camel_folder_refresh_info:
 * @folder: a #CamelFolder
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously synchronizes a folder's summary with its backing store.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_folder_refresh_info_finish() to get the result of the operation.
 *
 * Since: 3.2
 **/
void
camel_folder_refresh_info (CamelFolder *folder,
                           gint io_priority,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GTask *task;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	task = g_task_new (folder, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_folder_refresh_info);
	g_task_set_priority (task, io_priority);

	g_task_run_in_thread (task, folder_refresh_info_thread);

	g_object_unref (task);
}

/**
 * camel_folder_refresh_info_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_folder_refresh_info().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.2
 **/
gboolean
camel_folder_refresh_info_finish (CamelFolder *folder,
                                  GAsyncResult *result,
                                  GError **error)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, folder), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_folder_refresh_info), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * camel_folder_synchronize_sync:
 * @folder: a #CamelFolder
 * @expunge: whether to expunge after synchronizing
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Synchronizes any changes that have been made to @folder to its
 * backing store, optionally expunging deleted messages as well.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_synchronize_sync (CamelFolder *folder,
                               gboolean expunge,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->synchronize_sync != NULL, FALSE);

	/* Need to connect the service before we can synchronize. */
	success = folder_maybe_connect_sync (folder, cancellable, error);
	if (!success)
		return FALSE;

	camel_folder_lock (folder);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder);
		return FALSE;
	}

	if (!(camel_folder_get_flags (folder) & CAMEL_FOLDER_HAS_BEEN_DELETED)) {
		success = class->synchronize_sync (
			folder, expunge, cancellable, error);
		CAMEL_CHECK_GERROR (folder, synchronize_sync, success, error);

		if (success && expunge)
			success = camel_folder_maybe_run_db_maintenance (folder, error);
	}

	camel_folder_unlock (folder);

	return success;
}

/* Helper for camel_folder_synchronize() */
static void
folder_synchronize_thread (GTask *task,
                           gpointer source_object,
                           gpointer task_data,
                           GCancellable *cancellable)
{
	gboolean success;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	success = camel_folder_synchronize_sync (
		CAMEL_FOLDER (source_object),
		async_context->expunge,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * camel_folder_synchronize:
 * @folder: a #CamelFolder
 * @expunge: whether to expunge after synchronizing
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Synchronizes any changes that have been made to @folder to its backing
 * store asynchronously, optionally expunging deleted messages as well.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_folder_synchronize_finish() to get the result of the operation.
 *
 * Since: 3.0
 **/
void
camel_folder_synchronize (CamelFolder *folder,
                          gboolean expunge,
                          gint io_priority,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	async_context = g_slice_new0 (AsyncContext);
	async_context->expunge = expunge;

	task = g_task_new (folder, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_folder_synchronize);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, folder_synchronize_thread);

	g_object_unref (task);
}

/**
 * camel_folder_synchronize_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_folder_synchronize().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_synchronize_finish (CamelFolder *folder,
                                 GAsyncResult *result,
                                 GError **error)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, folder), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_folder_synchronize), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * camel_folder_synchronize_message_sync:
 * @folder: a #CamelFolder
 * @message_uid: a message UID
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Ensure that a message identified by @message_uid has been synchronized in
 * @folder so that calling camel_folder_get_message() on it later will work
 * in offline mode.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_synchronize_message_sync (CamelFolder *folder,
                                       const gchar *message_uid,
                                       GCancellable *cancellable,
                                       GError **error)
{
	CamelFolderClass *class;
	gboolean success = FALSE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (message_uid != NULL, FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->get_message_sync != NULL, FALSE);

	camel_folder_lock (folder);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder);
		return FALSE;
	}

	/* Use the sync_message method if the class implements it. */
	if (class->synchronize_message_sync != NULL) {
		success = class->synchronize_message_sync (
			folder, message_uid, cancellable, error);
		CAMEL_CHECK_GERROR (
			folder, synchronize_message_sync, success, error);
	} else {
		CamelMimeMessage *message;

		message = class->get_message_sync (
			folder, message_uid, cancellable, error);
		CAMEL_CHECK_GERROR (
			folder, get_message_sync, message != NULL, error);

		if (message != NULL) {
			g_object_unref (message);
			success = TRUE;
		}
	}

	camel_folder_unlock (folder);

	return success;
}

/* Helper for camel_folder_synchronize_message() */
static void
folder_synchronize_message_thread (GTask *task,
                                   gpointer source_object,
                                   gpointer task_data,
                                   GCancellable *cancellable)
{
	gboolean success;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	success = camel_folder_synchronize_message_sync (
		CAMEL_FOLDER (source_object),
		async_context->message_uid,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * camel_folder_synchronize_message:
 * @folder: a #CamelFolder
 * @message_uid: a message UID
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously ensure that a message identified by @message_uid has been
 * synchronized in @folder so that calling camel_folder_get_message() on it
 * later will work in offline mode.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_folder_synchronize_message_finish() to get the result of the
 * operation.
 *
 * Since: 3.0
 **/
void
camel_folder_synchronize_message (CamelFolder *folder,
                                  const gchar *message_uid,
                                  gint io_priority,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (message_uid != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->message_uid = g_strdup (message_uid);

	task = g_task_new (folder, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_folder_synchronize_message);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, folder_synchronize_message_thread);

	g_object_unref (task);
}

/**
 * camel_folder_synchronize_message_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_folder_synchronize_message().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_synchronize_message_finish (CamelFolder *folder,
                                         GAsyncResult *result,
                                         GError **error)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, folder), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_folder_synchronize_message), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

typedef struct _UidIndexPair {
	GPtrArray *uids; /* gchar * */
	GPtrArray *indexes; /* GUINT_TO_POINTER () */
} UidIndexPair;

static void
uid_index_pair_free (gpointer ptr)
{
	UidIndexPair *uip = ptr;

	if (uip) {
		g_ptr_array_unref (uip->uids);
		if (uip->indexes)
			g_ptr_array_unref (uip->indexes);
		g_slice_free (UidIndexPair, uip);
	}
}

/**
 * camel_folder_transfer_messages_to_sync:
 * @source: the source #CamelFolder
 * @message_uids: (element-type utf8): message UIDs in @source
 * @destination: the destination #CamelFolder
 * @delete_originals: whether or not to delete the original messages
 * @transferred_uids: (element-type utf8) (out) (optional) (nullable) (transfer container): if
 *                    non-%NULL, the UIDs of the resulting messages in
 *                    @destination will be stored here, if known.
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Copies or moves messages from one folder to another.  If the
 * @source and @destination folders have the same parent_store, this
 * may be more efficient than using camel_folder_append_message_sync().
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.0
 **/
gboolean
camel_folder_transfer_messages_to_sync (CamelFolder *source,
                                        GPtrArray *message_uids,
                                        CamelFolder *destination,
                                        gboolean delete_originals,
                                        GPtrArray **transferred_uids,
                                        GCancellable *cancellable,
                                        GError **error)
{
	CamelFolderClass *class;
	CamelStore *source_store;
	CamelStore *destination_store;
	gboolean success, done = FALSE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (source), FALSE);
	g_return_val_if_fail (CAMEL_IS_FOLDER (destination), FALSE);
	g_return_val_if_fail (message_uids != NULL, FALSE);

	if (source == destination || message_uids->len == 0)
		return TRUE;

	source_store = camel_folder_get_parent_store (source);
	destination_store = camel_folder_get_parent_store (destination);

	/* Need to connect both services before we can transfer. */
	success = folder_maybe_connect_sync (destination, cancellable, error);
	if (success && source_store != destination_store)
		success = folder_maybe_connect_sync (source, cancellable, error);
	if (!success)
		return FALSE;

	if (source_store == destination_store) {
		/* If either folder is a vtrash, we need to use the
		 * vtrash transfer method. */
		if (CAMEL_IS_VTRASH_FOLDER (destination))
			class = CAMEL_FOLDER_GET_CLASS (destination);
		else
			class = CAMEL_FOLDER_GET_CLASS (source);

		g_return_val_if_fail (class != NULL, FALSE);

		success = class->transfer_messages_to_sync (
			source, message_uids, destination, delete_originals,
			transferred_uids, cancellable, error);

		done = TRUE;
	}

	/* When the source folder is a virtual folder then split the transfer operation
	   to respective real folder(s). */
	if (!done && CAMEL_IS_VEE_FOLDER (source)) {
		GHashTable *todo; /* CamelFolder * ~> UidIndexPair * */
		CamelVeeFolder *vfolder = CAMEL_VEE_FOLDER (source);
		guint ii;

		todo = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, uid_index_pair_free);

		for (ii = 0; ii < message_uids->len; ii++) {
			CamelMessageInfo *nfo = camel_folder_get_message_info (source, message_uids->pdata[ii]);
			CamelFolder *folder;
			gchar *real_uid = NULL;

			if (!nfo)
				continue;

			folder = camel_vee_folder_get_location (vfolder, CAMEL_VEE_MESSAGE_INFO (nfo), &real_uid);
			if (folder && real_uid) {
				UidIndexPair *uip;

				uip = g_hash_table_lookup (todo, folder);
				if (!uip) {
					uip = g_slice_new0 (UidIndexPair);
					uip->uids = g_ptr_array_new_with_free_func ((GDestroyNotify) camel_pstring_free);
					if (transferred_uids)
						uip->indexes = g_ptr_array_new ();

					g_hash_table_insert (todo, g_object_ref (folder), uip);
				}

				g_ptr_array_add (uip->uids, (gpointer) camel_pstring_strdup (real_uid));
				if (uip->indexes)
					g_ptr_array_add (uip->indexes, GUINT_TO_POINTER (ii));
			}

			g_object_unref (nfo);
			g_free (real_uid);
		}

		done = g_hash_table_size (todo) > 0;

		if (done) {
			GHashTableIter iter;
			gpointer key, value;

			if (transferred_uids) {
				*transferred_uids = g_ptr_array_new_with_free_func (g_free);
				g_ptr_array_set_size (*transferred_uids, message_uids->len);
			}

			g_hash_table_iter_init (&iter, todo);
			while (g_hash_table_iter_next (&iter, &key, &value) && success) {
				CamelFolder *folder = key;
				UidIndexPair *uip = value;
				GPtrArray *transferred = NULL;

				source_store = camel_folder_get_parent_store (folder);

				if (source_store == destination_store) {
					/* If either folder is a vtrash, we need to use the
					 * vtrash transfer method. */
					if (CAMEL_IS_VTRASH_FOLDER (destination))
						class = CAMEL_FOLDER_GET_CLASS (destination);
					else
						class = CAMEL_FOLDER_GET_CLASS (folder);

					g_warn_if_fail (class != NULL);

					success = class && class->transfer_messages_to_sync (
						folder, uip->uids, destination, delete_originals,
						transferred_uids ? &transferred : NULL, cancellable, error);
				} else {
					success = folder_transfer_messages_to_sync (
						folder, uip->uids, destination, delete_originals,
						transferred_uids ? &transferred : NULL, cancellable, error);
				}

				if (transferred) {
					g_warn_if_fail (transferred->len != uip->indexes->len);

					for (ii = 0; ii < transferred->len && ii < uip->indexes->len; ii++) {
						guint idx = GPOINTER_TO_UINT (uip->indexes->pdata[ii]);

						g_warn_if_fail (idx < (*transferred_uids)->len);

						if (idx < (*transferred_uids)->len) {
							(*transferred_uids)->pdata[idx] = transferred->pdata[ii];
							transferred->pdata[ii] = NULL;
						}
					}

					g_ptr_array_free (transferred, TRUE);
				}
			}

			if (!success && transferred_uids) {
				g_ptr_array_free (*transferred_uids, TRUE);
				*transferred_uids = NULL;
			}
		}

		g_hash_table_destroy (todo);
	}

	if (!done) {
		success = folder_transfer_messages_to_sync (
			source, message_uids, destination, delete_originals,
			transferred_uids, cancellable, error);
	}

	return success;
}

/* Helper for folder_transfer_messages_to_thread() */
static void
folder_transfer_messages_to_thread (GTask *task,
                                    gpointer source_object,
                                    gpointer task_data,
                                    GCancellable *cancellable)
{
	gboolean success;
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = (AsyncContext *) task_data;

	success = camel_folder_transfer_messages_to_sync (
		CAMEL_FOLDER (source_object),
		async_context->message_uids,
		async_context->destination,
		async_context->delete_originals,
		&async_context->transferred_uids,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * camel_folder_transfer_messages_to:
 * @source: the source #CamelFolder
 * @message_uids: (element-type utf8): message UIDs in @source
 * @destination: the destination #CamelFolder
 * @delete_originals: whether or not to delete the original messages
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously copies or moves messages from one folder to another.
 * If the @source or @destination folders have the same parent store,
 * this may be more efficient than using camel_folder_append_message().
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_folder_transfer_messages_to_finish() to get the result of the
 * operation.
 *
 * Since: 3.0
 **/
void
camel_folder_transfer_messages_to (CamelFolder *source,
                                   GPtrArray *message_uids,
                                   CamelFolder *destination,
                                   gboolean delete_originals,
                                   gint io_priority,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;
	guint ii;

	g_return_if_fail (CAMEL_IS_FOLDER (source));
	g_return_if_fail (CAMEL_IS_FOLDER (destination));
	g_return_if_fail (message_uids != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->message_uids = g_ptr_array_new_full (message_uids->len, (GDestroyNotify) camel_pstring_free);
	async_context->destination = g_object_ref (destination);
	async_context->delete_originals = delete_originals;

	for (ii = 0; ii < message_uids->len; ii++)
		g_ptr_array_add (
			async_context->message_uids,
			(gpointer) camel_pstring_strdup (message_uids->pdata[ii]));

	task = g_task_new (source, cancellable, callback, user_data);
	g_task_set_source_tag (task, camel_folder_transfer_messages_to);
	g_task_set_priority (task, io_priority);

	g_task_set_task_data (
		task, async_context,
		(GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, folder_transfer_messages_to_thread);

	g_object_unref (task);
}

/**
 * camel_folder_transfer_messages_to_finish:
 * @source: a #CamelFolder
 * @result: a #GAsyncResult
 * @transferred_uids: (element-type utf8) (out) (optional) (nullable) (transfer container): if
 *                    non-%NULL, the UIDs of the resulting messages in
 *                    @destination will be stored here, if known.
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_folder_transfer_messages_to().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_transfer_messages_to_finish (CamelFolder *source,
                                          GAsyncResult *result,
                                          GPtrArray **transferred_uids,
                                          GError **error)
{
	AsyncContext *async_context;

	g_return_val_if_fail (CAMEL_IS_FOLDER (source), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, source), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, camel_folder_transfer_messages_to), FALSE);

	async_context = g_task_get_task_data (G_TASK (result));

	if (!g_task_had_error (G_TASK (result))) {
		if (transferred_uids != NULL) {
			*transferred_uids = async_context->transferred_uids;
			async_context->transferred_uids = NULL;
		}
	}

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * camel_folder_prepare_content_refresh:
 * @folder: a #CamelFolder
 *
 * Lets the @folder know that it should refresh its content
 * the next time from fresh. This is useful for remote accounts,
 * to fully re-check the folder content against the server.
 *
 * Since: 3.22
 **/
void
camel_folder_prepare_content_refresh (CamelFolder *folder)
{
	CamelFolderClass *klass;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	klass = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (klass != NULL);

	if (klass->prepare_content_refresh)
		klass->prepare_content_refresh (folder);
}

/**
 * camel_folder_dup_headers_sync:
 * @folder: a #CamelFolder
 * @uid: a message UID
 * @out_headers: (out) (transfer full): return location to set read #CamelNameValueArray to
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Reads headers of a message with the @uid and returns it
 * in the @out_headers. Free the headers with camel_name_value_array_free(),
 * when no longer needed.
 *
 * This is an optional method, which is meant to be used by the providers
 * which can read the headers from the server when not available locally.
 * The default implementation tries to read the headers from a loaded
 * message info and a locally cached message when its file name is known.
 * It returns a G_IO_ERROR_NOT_FOUND error when failed.
 *
 * Returns: whether the headers had been found and the @out_headers populated
 *
 * Since: 3.58
 **/
gboolean
camel_folder_dup_headers_sync (CamelFolder *folder,
			       const gchar *uid,
			       CamelNameValueArray **out_headers,
			       GCancellable *cancellable,
			       GError **error)
{
	CamelFolderClass *klass;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_headers != NULL, FALSE);

	klass = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->dup_headers_sync != NULL, FALSE);

	return klass->dup_headers_sync (folder, uid, out_headers, cancellable, error);
}

/**
 * camel_folder_search_header_sync:
 * @folder: a #CamelFolder
 * @header_name: a header name to search
 * @words: (nullable) (element-type utf8): a list of words to search for, or %NULL to check an existence of the header instead
 * @out_uids: (out) (transfer container) (element-type utf8): a #GPtrArray of the satisfying message UIDs
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Search the folder for messages with header @header_name, which either contains
 * the @words or, when the @words is empty or %NULL, the header exists in the message.
 * The list of satisfying message UIDs is returned in the @out_uids.
 * The result list can be empty, meaning no such message exists.
 *
 * Free the returned @out_uids with g_ptr_array_unref(), when no longer needed.
 *
 * This is an optional helper method, meant to search server-side. The default
 * implementation returns a G_IO_ERROR_NOT_SUPPORTED error.
 *
 * Returns: whether could search and set the @out_uids.
 *
 * Since: 3.58
 **/
gboolean
camel_folder_search_header_sync (CamelFolder *folder,
				 const gchar *header_name,
				 /* const */ GPtrArray *words, /* gchar * */
				 GPtrArray **out_uids, /* gchar * */
				 GCancellable *cancellable,
				 GError **error)
{
	CamelFolderClass *klass;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (header_name != NULL, FALSE);
	g_return_val_if_fail (out_uids != NULL, FALSE);

	klass = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->search_header_sync != NULL, FALSE);

	return klass->search_header_sync (folder, header_name, words, out_uids, cancellable, error);
}

/**
 * camel_folder_search_body_sync:
 * @folder: a #CamelFolder
 * @words: (element-type utf8): a list of words to search for, or %NULL to check an existence of the header instead
 * @out_uids: (out) (transfer container) (element-type utf8): a #GPtrArray of the satisfying message UIDs
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Search the folder for messages with body containing @words.
 * The list of satisfying message UIDs is returned in the @out_uids.
 * The result list can be empty, meaning no such message exists.
 *
 * Free the returned @out_uids with g_ptr_array_unref(), when no longer needed.
 *
 * This is an optional helper method, meant to search server-side. The default
 * implementation returns a G_IO_ERROR_NOT_SUPPORTED error.
 *
 * Returns: whether could search and set the @out_uids.
 *
 * Since: 3.58
 **/
gboolean
camel_folder_search_body_sync (CamelFolder *folder,
			       /* const */ GPtrArray *words, /* gchar * */
			       GPtrArray **out_uids, /* gchar * */
			       GCancellable *cancellable,
			       GError **error)
{
	CamelFolderClass *klass;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (words != NULL, FALSE);
	g_return_val_if_fail (out_uids != NULL, FALSE);

	klass = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->search_body_sync != NULL, FALSE);

	return klass->search_body_sync (folder, words, out_uids, cancellable, error);
}

/**
 * camel_folder_take_state_filename:
 * @self: a #CamelFolder
 * @filename: (transfer full): the state filename
 *
 * Set the current state filename.
 *
 * Since: 3.58
 **/
void
camel_folder_take_state_filename (CamelFolder *self,
				  gchar *filename)
{
	g_return_if_fail (CAMEL_IS_FOLDER (self));

	g_free (self->priv->state_filename);
	self->priv->state_filename = filename;
}

/**
 * camel_folder_get_state_filename:
 * @self: a #CamelFolder
 *
 * Get the current state filename.
 *
 * Returns: (nullable): the state filename.
 *
 * Since: 3.58
 **/
const gchar *
camel_folder_get_state_filename (CamelFolder *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (self), NULL);

	return self->priv->state_filename;
}

/**
 * camel_folder_save_state:
 * @self: a #CamelFolder
 *
 * Saves properties with #CAMEL_FOLDER_PARAM_PERMANENT into a state file previously
 * set by camel_folder_take_state_filename(). The function does nothing when no
 * state file is set.
 *
 * Any errors are reported on the terminal, because they are meant non-fatal and
 * rather informative.
 *
 * Since: 3.58
 **/
void
camel_folder_save_state (CamelFolder *self)
{
	GObjectClass *class;
	GParamSpec **all_properties;
	guint ii, n_properties = 0;
	GError *local_error = NULL;
	GKeyFile *key_file;
	gchar *start_group;

	g_return_if_fail (CAMEL_IS_FOLDER (self));

	if (!self->priv->state_filename)
		return;

	key_file = g_key_file_new ();
	class = G_OBJECT_GET_CLASS (G_OBJECT (self));
	all_properties = g_object_class_list_properties (class, &n_properties);
	for (ii = 0; ii < n_properties; ii++) {
		GParamSpec *pspec = all_properties[ii];
		gboolean key_val_b = FALSE;
		gint key_val_i = 0;
		gint64 key_val_i64 = 0;
		gchar *key_val_s = NULL;

		if ((pspec->flags & CAMEL_FOLDER_PARAM_PERSISTENT) == 0)
			continue;

		switch (pspec->value_type) {
		case G_TYPE_BOOLEAN:
			g_object_get (G_OBJECT (self), pspec->name, &key_val_b, NULL);
			if (((GParamSpecBoolean *)pspec)->default_value != key_val_b) {
				g_key_file_set_boolean (key_file, g_type_name (pspec->owner_type), pspec->name, key_val_b);
			}

			break;
		case G_TYPE_INT:
			g_object_get (G_OBJECT (self), pspec->name, &key_val_i, NULL);
			if (((GParamSpecInt *)pspec)->default_value != key_val_i) {
				g_key_file_set_integer (key_file, g_type_name (pspec->owner_type), pspec->name, key_val_i);
			}

			break;
		case G_TYPE_INT64:
			g_object_get (G_OBJECT (self), pspec->name, &key_val_i64, NULL);
			if (((GParamSpecInt64 *)pspec)->default_value != key_val_i64) {
				g_key_file_set_int64 (key_file, g_type_name (pspec->owner_type), pspec->name, key_val_i64);
			}

			break;
		case G_TYPE_STRING:
			g_object_get (G_OBJECT (self), pspec->name, &key_val_s, NULL);
			if (g_strcmp0 (((GParamSpecString *)pspec)->default_value, key_val_s)) {
				g_key_file_set_string (key_file, g_type_name (pspec->owner_type), pspec->name, key_val_s);
			}

			g_free (key_val_s);
			break;
		default:
			if (g_type_is_a (pspec->value_type, G_TYPE_ENUM)) {
				gint key_val_e;
				g_object_get (G_OBJECT (self), pspec->name, &key_val_e, NULL);
				if (((GParamSpecEnum *)pspec)->default_value != key_val_e) {
					GEnumValue *enum_value;
					enum_value = g_enum_get_value (((GParamSpecEnum *)pspec)->enum_class, key_val_e);
					if (enum_value) {
						g_key_file_set_string (key_file, g_type_name (pspec->owner_type), pspec->name, enum_value->value_name);
					}
				}
			} else {
				g_warn_if_reached ();
			}
			break;
		}
	}

	g_clear_pointer (&all_properties, g_free);
	/* Do not save and empty file */
	start_group = g_key_file_get_start_group (key_file);
	if (!start_group) {
		g_unlink (self->priv->state_filename);
		g_clear_pointer (&key_file, g_key_file_free);
		return;
	}

	g_clear_pointer (&start_group, g_free);
	if (!g_key_file_save_to_file (key_file, self->priv->state_filename, &local_error)) {
		g_warning ("Unable to save '%s': %s", self->priv->state_filename, local_error ? local_error->message : "Unknown error");
		g_clear_error (&local_error);
	}

	g_clear_pointer (&key_file, g_key_file_free);
	return;
}

/**
 * camel_folder_load_state:
 * @self: a #CamelFolder
 *
 * Loads properties with #CAMEL_FOLDER_PARAM_PERMANENT from a state file previously
 * set by camel_folder_take_state_filename(). The function does nothing when no
 * state file is set.
 *
 * Any errors are reported on the terminal, because they are meant non-fatal and
 * rather informative.
 *
 * Since: 3.58
 **/
void
camel_folder_load_state (CamelFolder *self)
{
	GObjectClass *class;
	GParamSpec **all_properties;
	guint ii, n_properties;
	GError *local_error = NULL;
	GKeyFile *key_file;

	g_return_if_fail (CAMEL_IS_FOLDER (self));

	if (!self->priv->state_filename)
		return;

	key_file = g_key_file_new ();
	if (!g_key_file_load_from_file (key_file, self->priv->state_filename, G_KEY_FILE_NONE, &local_error)) {
		g_clear_pointer (&key_file, g_key_file_free);
		if (!g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
			if (folder_read_legacy_state (self, self->priv->state_filename)) {
				camel_folder_save_state (self);
			} else {
				g_warning ("Unable to read state file '%s'", self->priv->state_filename);
			}
		}

		g_clear_error (&local_error);
		return;
	}

	class = G_OBJECT_GET_CLASS (G_OBJECT (self));
	all_properties = g_object_class_list_properties (class, &n_properties);
	for (ii = 0; ii < n_properties; ii++) {
		GParamSpec *pspec = all_properties[ii];
		GValue value = G_VALUE_INIT;
		gboolean key_val_b;
		gint key_val_i;
		gint64 key_val_i64;
		gchar *key_val_s;

		if ((pspec->flags & CAMEL_FOLDER_PARAM_PERSISTENT) == 0)
			continue;

		g_value_init (&value, pspec->value_type);
		switch (pspec->value_type) {
		case G_TYPE_BOOLEAN:
			key_val_b = g_key_file_get_boolean (key_file, g_type_name (pspec->owner_type), pspec->name, &local_error);
			if (!local_error) {
				g_value_set_boolean (&value, key_val_b);
				g_object_set_property (G_OBJECT (self), pspec->name, &value);
			}

			g_clear_error (&local_error);
			break;
		case G_TYPE_INT:
			key_val_i = g_key_file_get_integer (key_file, g_type_name (pspec->owner_type), pspec->name, &local_error);
			if (!local_error) {
				g_value_set_int (&value, key_val_i);
				g_object_set_property (G_OBJECT (self), pspec->name, &value);
			}

			g_clear_error (&local_error);
			break;
		case G_TYPE_INT64:
			key_val_i64 = g_key_file_get_int64 (key_file, g_type_name (pspec->owner_type), pspec->name, &local_error);
			if (!local_error) {
				g_value_set_int64 (&value, key_val_i64);
				g_object_set_property (G_OBJECT (self), pspec->name, &value);
			}

			g_clear_error (&local_error);
			break;
		case G_TYPE_STRING:
			key_val_s = g_key_file_get_string (key_file, g_type_name (pspec->owner_type), pspec->name, &local_error);
			if (!local_error) {
				g_value_take_string (&value, g_steal_pointer (&key_val_s));
				g_object_set_property (G_OBJECT (self), pspec->name, &value);
			}

			g_clear_error (&local_error);
			break;
		default:
			if (g_type_is_a (pspec->value_type, G_TYPE_ENUM)) {
				gchar *enum_name = g_key_file_get_string (key_file, g_type_name (pspec->owner_type), pspec->name, &local_error);
				if (!local_error) {
					GEnumValue *enum_value;

					enum_value = g_enum_get_value_by_name (((GParamSpecEnum *)pspec)->enum_class, enum_name);
					if (enum_value) {
						g_value_set_enum (&value, enum_value->value);
						g_object_set_property (G_OBJECT (self), pspec->name, &value);
					}

					g_clear_pointer (&enum_name, g_free);
				}

				g_clear_error (&local_error);
			} else {
				g_warn_if_reached ();
			}
			break;
		}
	}

	g_clear_pointer (&key_file, g_key_file_free);
	g_free (all_properties);
	return;
}

/**
 * camel_folder_class_map_legacy_property:
 * @folder_class: the class to add the property mapping
 * @prop_name: (transfer none): the static property name
 * @tag: the legacy property tag
 *
 * Add the legacy property binding to allow opening legacy binary file states.
 *
 * Since: 3.58
 **/
void
camel_folder_class_map_legacy_property (CamelFolderClass *folder_class,
					const gchar *prop_name,
					gint32 tag)
{
	CamelFolderClassPrivate *priv;
	FolderStateMapping *fsm;

	priv = G_TYPE_CLASS_GET_PRIVATE (folder_class, CAMEL_TYPE_FOLDER, CamelFolderClassPrivate);
	if (priv->n_state_mapping >= G_N_ELEMENTS (priv->state_mapping)) {
		g_warning ("Cannot set legacy property map of '%s' on '%s': Too many legacy properties mapped.",
			prop_name, g_type_name_from_class ((GTypeClass *) folder_class));
		return;
	}

	fsm = &priv->state_mapping[priv->n_state_mapping];
	fsm->prop_name = prop_name;
	fsm->tag = tag;
	priv->n_state_mapping++;
}

G_DEFINE_BOXED_TYPE (CamelFolderChangeInfo, camel_folder_change_info, camel_folder_change_info_copy, camel_folder_change_info_free)

/**
 * camel_folder_change_info_new:
 *
 * Create a new folder change info structure.
 *
 * Change info structures are not MT-SAFE and must be
 * locked for exclusive access externally.
 *
 * Returns: (transfer full): a new #CamelFolderChangeInfo
 **/
CamelFolderChangeInfo *
camel_folder_change_info_new (void)
{
	CamelFolderChangeInfo *info;

	info = g_slice_new (CamelFolderChangeInfo);
	info->uid_added = g_ptr_array_new ();
	info->uid_removed = g_ptr_array_new ();
	info->uid_changed = g_ptr_array_new ();
	info->uid_recent = g_ptr_array_new ();
	info->priv = g_slice_new (struct _CamelFolderChangeInfoPrivate);
	info->priv->uid_stored = g_hash_table_new (g_str_hash, g_str_equal);
	info->priv->uid_source = NULL;
	info->priv->uid_filter = g_ptr_array_new ();
	info->priv->uid_pool = camel_mempool_new (512, 256, CAMEL_MEMPOOL_ALIGN_BYTE);

	return info;
}

/**
 * camel_folder_change_info_copy:
 * @src: a #CamelFolderChangeInfo to make copy of
 *
 * Creates a copy of the @src.
 *
 * Returns: (transfer full): Copy of the @src.
 *
 * Since: 3.24
 **/
CamelFolderChangeInfo *
camel_folder_change_info_copy (CamelFolderChangeInfo *src)
{
	CamelFolderChangeInfo *copy;

	if (!src)
		return NULL;

	copy = camel_folder_change_info_new ();
	camel_folder_change_info_cat (copy, src);

	return copy;
}

/**
 * camel_folder_change_info_add_source:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a source uid for generating a changeset.
 **/
void
camel_folder_change_info_add_source (CamelFolderChangeInfo *info,
                                     const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_return_if_fail (info != NULL);
	g_return_if_fail (uid != NULL);

	p = info->priv;

	if (p->uid_source == NULL)
		p->uid_source = g_hash_table_new (g_str_hash, g_str_equal);

	if (g_hash_table_lookup (p->uid_source, uid) == NULL)
		g_hash_table_insert (p->uid_source, camel_mempool_strdup (p->uid_pool, uid), GINT_TO_POINTER (1));
}

/**
 * camel_folder_change_info_add_source_list:
 * @info: a #CamelFolderChangeInfo
 * @list: (element-type utf8) (transfer container): a list of uids
 *
 * Add a list of source uid's for generating a changeset.
 **/
void
camel_folder_change_info_add_source_list (CamelFolderChangeInfo *info,
                                          const GPtrArray *list)
{
	struct _CamelFolderChangeInfoPrivate *p;
	gint i;

	g_return_if_fail (info != NULL);
	g_return_if_fail (list != NULL);

	p = info->priv;

	if (p->uid_source == NULL)
		p->uid_source = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < list->len; i++) {
		gchar *uid = list->pdata[i];

		if (g_hash_table_lookup (p->uid_source, uid) == NULL)
			g_hash_table_insert (p->uid_source, camel_mempool_strdup (p->uid_pool, uid), GINT_TO_POINTER (1));
	}
}

/**
 * camel_folder_change_info_add_update:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a uid from the updated list, used to generate a changeset diff.
 **/
void
camel_folder_change_info_add_update (CamelFolderChangeInfo *info,
                                     const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	gchar *key;
	gint value;

	g_return_if_fail (info != NULL);
	g_return_if_fail (uid != NULL);

	p = info->priv;

	if (p->uid_source == NULL) {
		camel_folder_change_info_add_uid (info, uid);
		return;
	}

	if (g_hash_table_lookup_extended (p->uid_source, uid, (gpointer) &key, (gpointer) &value)) {
		g_hash_table_remove (p->uid_source, key);
	} else {
		camel_folder_change_info_add_uid (info, uid);
	}
}

/**
 * camel_folder_change_info_add_update_list:
 * @info: a #CamelFolderChangeInfo
 * @list: (element-type utf8) (transfer container): a list of uids
 *
 * Add a list of uid's from the updated list.
 **/
void
camel_folder_change_info_add_update_list (CamelFolderChangeInfo *info,
                                          const GPtrArray *list)
{
	gint i;

	g_return_if_fail (info != NULL);
	g_return_if_fail (list != NULL);

	for (i = 0; i < list->len; i++)
		camel_folder_change_info_add_update (info, list->pdata[i]);
}

static void
change_info_remove (gchar *key,
                    gpointer value,
                    CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p = info->priv;
	GPtrArray *olduids;
	gchar *olduid;

	if (g_hash_table_lookup_extended (p->uid_stored, key, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if it was added/changed them removed, then remove it */
		if (olduids != info->uid_removed) {
			g_ptr_array_remove_fast (olduids, olduid);
			g_ptr_array_add (info->uid_removed, olduid);
			g_hash_table_insert (p->uid_stored, olduid, info->uid_removed);
		}
		return;
	}

	/* we don't need to copy this, as they've already been copied into our pool */
	g_ptr_array_add (info->uid_removed, key);
	g_hash_table_insert (p->uid_stored, key, info->uid_removed);
}

/**
 * camel_folder_change_info_build_diff:
 * @info: a #CamelFolderChangeInfo
 *
 * Compare the source uid set to the updated uid set and generate the
 * differences into the added and removed lists.
 **/
void
camel_folder_change_info_build_diff (CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_return_if_fail (info != NULL);

	p = info->priv;

	if (p->uid_source) {
		g_hash_table_foreach (p->uid_source, (GHFunc) change_info_remove, info);
		g_hash_table_destroy (p->uid_source);
		p->uid_source = NULL;
	}
}

static void
change_info_recent_uid (CamelFolderChangeInfo *info,
                        const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	gchar *olduid;

	p = info->priv;

	/* always add to recent, but don't let anyone else know */
	if (!g_hash_table_lookup_extended (p->uid_stored, uid, (gpointer *) &olduid, (gpointer *) &olduids)) {
		olduid = camel_mempool_strdup (p->uid_pool, uid);
	}
	g_ptr_array_add (info->uid_recent, olduid);
}

static void
change_info_filter_uid (CamelFolderChangeInfo *info,
                        const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	gchar *olduid;

	p = info->priv;

	/* always add to filter, but don't let anyone else know */
	if (!g_hash_table_lookup_extended (p->uid_stored, uid, (gpointer *) &olduid, (gpointer *) &olduids)) {
		olduid = camel_mempool_strdup (p->uid_pool, uid);
	}
	g_ptr_array_add (p->uid_filter, olduid);
}

static void
change_info_cat (CamelFolderChangeInfo *info,
                 GPtrArray *source,
                 void (*add)(CamelFolderChangeInfo *info,
                 const gchar *uid))
{
	gint i;

	for (i = 0; i < source->len; i++)
		add (info, source->pdata[i]);
}

/**
 * camel_folder_change_info_cat:
 * @info: a #CamelFolderChangeInfo to append to
 * @src: a #CamelFolderChangeInfo to append from
 *
 * Concatenate one change info onto antoher. Can be used to copy them
 * too.
 **/
void
camel_folder_change_info_cat (CamelFolderChangeInfo *info,
                              CamelFolderChangeInfo *src)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (src != NULL);

	change_info_cat (info, src->uid_added, camel_folder_change_info_add_uid);
	change_info_cat (info, src->uid_removed, camel_folder_change_info_remove_uid);
	change_info_cat (info, src->uid_changed, camel_folder_change_info_change_uid);
	change_info_cat (info, src->uid_recent, change_info_recent_uid);
	change_info_cat (info, src->priv->uid_filter, change_info_filter_uid);
}

/**
 * camel_folder_change_info_add_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a new uid to the changeinfo.
 **/
void
camel_folder_change_info_add_uid (CamelFolderChangeInfo *info,
                                  const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	gchar *olduid;

	g_return_if_fail (info != NULL);
	g_return_if_fail (uid != NULL);

	p = info->priv;

	if (g_hash_table_lookup_extended (p->uid_stored, uid, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if it was removed then added, promote it to a changed */
		/* if it was changed then added, make it added */
		if (olduids == info->uid_removed) {
			g_ptr_array_remove_fast (olduids, olduid);
			g_ptr_array_add (info->uid_changed, olduid);
			g_hash_table_insert (p->uid_stored, olduid, info->uid_changed);
		} else if (olduids == info->uid_changed) {
			g_ptr_array_remove_fast (olduids, olduid);
			g_ptr_array_add (info->uid_added, olduid);
			g_hash_table_insert (p->uid_stored, olduid, info->uid_added);
		}
		return;
	}

	olduid = camel_mempool_strdup (p->uid_pool, uid);
	g_ptr_array_add (info->uid_added, olduid);
	g_hash_table_insert (p->uid_stored, olduid, info->uid_added);
}

/**
 * camel_folder_change_info_remove_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a uid to the removed uid list.
 **/
void
camel_folder_change_info_remove_uid (CamelFolderChangeInfo *info,
                                     const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	gchar *olduid;

	g_return_if_fail (info != NULL);
	g_return_if_fail (uid != NULL);

	p = info->priv;

	if (g_hash_table_lookup_extended (p->uid_stored, uid, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if it was added/changed them removed, then remove it */
		if (olduids != info->uid_removed) {
			g_ptr_array_remove_fast (olduids, olduid);
			g_ptr_array_add (info->uid_removed, olduid);
			g_hash_table_insert (p->uid_stored, olduid, info->uid_removed);
		}
		return;
	}

	olduid = camel_mempool_strdup (p->uid_pool, uid);
	g_ptr_array_add (info->uid_removed, olduid);
	g_hash_table_insert (p->uid_stored, olduid, info->uid_removed);
}

/**
 * camel_folder_change_info_change_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a uid to the changed uid list.
 **/
void
camel_folder_change_info_change_uid (CamelFolderChangeInfo *info,
                                     const gchar *uid)
{
	struct _CamelFolderChangeInfoPrivate *p;
	GPtrArray *olduids;
	gchar *olduid;

	g_return_if_fail (info != NULL);
	g_return_if_fail (uid != NULL);

	p = info->priv;

	if (g_hash_table_lookup_extended (p->uid_stored, uid, (gpointer) &olduid, (gpointer) &olduids)) {
		/* if we have it already, leave it as that */
		return;
	}

	olduid = camel_mempool_strdup (p->uid_pool, uid);
	g_ptr_array_add (info->uid_changed, olduid);
	g_hash_table_insert (p->uid_stored, olduid, info->uid_changed);
}

/**
 * camel_folder_change_info_recent_uid:
 * @info: a #CamelFolderChangeInfo
 * @uid: a uid
 *
 * Add a recent uid to the changedinfo.
 * This will also add the uid to the uid_filter array for potential
 * filtering
 **/
void
camel_folder_change_info_recent_uid (CamelFolderChangeInfo *info,
                                     const gchar *uid)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (uid != NULL);

	change_info_recent_uid (info, uid);
	change_info_filter_uid (info, uid);
}

/**
 * camel_folder_change_info_changed:
 * @info: a #CamelFolderChangeInfo
 *
 * Gets whether or not there have been any changes.
 *
 * Returns: %TRUE if the changeset contains any changes or %FALSE
 * otherwise
 **/
gboolean
camel_folder_change_info_changed (CamelFolderChangeInfo *info)
{
	g_return_val_if_fail (info != NULL, FALSE);

	return (info->uid_added->len || info->uid_removed->len || info->uid_changed->len || info->uid_recent->len);
}

/**
 * camel_folder_change_info_get_added_uids:
 * @info: a #CamelFolderChangeInfo
 *
 * Returns an array of added messages UIDs. The returned array, the same as its content,
 * is owned by the @info.
 *
 * Returns: (element-type utf8) (transfer none): An array of added UIDs.
 *
 * Since: 3.24
 **/
GPtrArray *
camel_folder_change_info_get_added_uids (CamelFolderChangeInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->uid_added;
}

/**
 * camel_folder_change_info_get_removed_uids:
 * @info: a #CamelFolderChangeInfo
 *
 * Returns an array of removed messages UIDs. The returned array, the same as its content,
 * is owned by the @info.
 *
 * Returns: (element-type utf8) (transfer none): An array of removed UIDs.
 *
 * Since: 3.24
 **/
GPtrArray *
camel_folder_change_info_get_removed_uids (CamelFolderChangeInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->uid_removed;
}

/**
 * camel_folder_change_info_get_changed_uids:
 * @info: a #CamelFolderChangeInfo
 *
 * Returns an array of changed messages UIDs. The returned array, the same as its content,
 * is owned by the @info.
 *
 * Returns: (element-type utf8) (transfer none): An array of changed UIDs.
 *
 * Since: 3.24
 **/
GPtrArray *
camel_folder_change_info_get_changed_uids (CamelFolderChangeInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->uid_changed;
}

/**
 * camel_folder_change_info_get_recent_uids:
 * @info: a #CamelFolderChangeInfo
 *
 * Returns an array of recent messages UIDs. The returned array, the same as its content,
 * is owned by the @info.
 *
 * Returns: (element-type utf8) (transfer none): An array of recent UIDs.
 *
 * Since: 3.24
 **/
GPtrArray *
camel_folder_change_info_get_recent_uids (CamelFolderChangeInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->uid_recent;
}

/**
 * camel_folder_change_info_clear:
 * @info: a #CamelFolderChangeInfo
 *
 * Empty out the change info; called after changes have been
 * processed.
 **/
void
camel_folder_change_info_clear (CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_return_if_fail (info != NULL);

	p = info->priv;

	g_ptr_array_set_size (info->uid_added, 0);
	g_ptr_array_set_size (info->uid_removed, 0);
	g_ptr_array_set_size (info->uid_changed, 0);
	g_ptr_array_set_size (info->uid_recent, 0);
	g_clear_pointer (&p->uid_source, g_hash_table_destroy);
	g_hash_table_destroy (p->uid_stored);
	p->uid_stored = g_hash_table_new (g_str_hash, g_str_equal);
	g_ptr_array_set_size (p->uid_filter, 0);
	camel_mempool_flush (p->uid_pool, TRUE);
}

/**
 * camel_folder_change_info_free:
 * @info: a #CamelFolderChangeInfo
 *
 * Free memory associated with the folder change info lists.
 **/
void
camel_folder_change_info_free (CamelFolderChangeInfo *info)
{
	struct _CamelFolderChangeInfoPrivate *p;

	g_return_if_fail (info != NULL);

	p = info->priv;

	if (p->uid_source)
		g_hash_table_destroy (p->uid_source);

	g_hash_table_destroy (p->uid_stored);
	g_ptr_array_free (p->uid_filter, TRUE);
	camel_mempool_destroy (p->uid_pool);
	g_slice_free (struct _CamelFolderChangeInfoPrivate, p);

	g_ptr_array_free (info->uid_added, TRUE);
	g_ptr_array_free (info->uid_removed, TRUE);
	g_ptr_array_free (info->uid_changed, TRUE);
	g_ptr_array_free (info->uid_recent, TRUE);
	g_slice_free (CamelFolderChangeInfo, info);
}

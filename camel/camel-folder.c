/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-folder.c: Abstract class for an email folder */

/*
 * Author:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-filter-driver.h"
#include "camel-folder.h"
#include "camel-mempool.h"
#include "camel-mime-message.h"
#include "camel-operation.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-vtrash-folder.h"
#include "camel-string-utils.h"

#define CAMEL_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_FOLDER, CamelFolderPrivate))

#define d(x)
#define w(x)

typedef struct _AsyncContext AsyncContext;
typedef struct _SignalData SignalData;
typedef struct _FolderFilterData FolderFilterData;

struct _CamelFolderPrivate {
	GStaticRecMutex lock;
	GStaticMutex change_lock;
	/* must require the 'change_lock' to access this */
	gint frozen;
	CamelFolderChangeInfo *changed_frozen; /* queues changed events */
	gboolean skip_folder_lock;

	/* Changes to be emitted from an idle callback. */
	CamelFolderChangeInfo *pending_changes;

	gpointer parent_store;  /* weak pointer */

	gchar *full_name;
	gchar *display_name;
	gchar *description;
};

struct _AsyncContext {
	/* arguments */
	CamelMimeMessage *message;  /* also a result */
	CamelMessageInfo *info;
	CamelFolder *destination;
	GPtrArray *message_uids;
	gchar *message_uid;         /* also a result */
	gboolean delete_originals;
	gboolean expunge;
	CamelFetchType fetch_type;
	gint limit;
	gchar *start_uid;
	gchar *end_uid;

	/* results */
	GPtrArray *transferred_uids;
	CamelFolderQuotaInfo *quota_info;
	gboolean success; /* A result that mention that there are more messages in fetch_messages operation */
};

struct _CamelFolderChangeInfoPrivate {
	GHashTable *uid_stored;	/* what we have stored, which array they're in */
	GHashTable *uid_source;	/* used to create unique lists */
	GPtrArray  *uid_filter; /* uids to be filtered */
	CamelMemPool *uid_pool;	/* pool used to store copies of uid strings */
};

struct _SignalData {
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

enum {
	PROP_0,
	PROP_DESCRIPTION,
	PROP_DISPLAY_NAME,
	PROP_FULL_NAME,
	PROP_PARENT_STORE
};

enum {
	CHANGED,
	DELETED,
	RENAMED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_ABSTRACT_TYPE (CamelFolder, camel_folder, CAMEL_TYPE_OBJECT)

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->message != NULL)
		g_object_unref (async_context->message);

	/* XXX This is actually an unref.  Good god, no wonder we
	 *     have so many crashes involving CamelMessageInfos! */
	if (async_context->info != NULL)
		camel_message_info_free (async_context->info);

	if (async_context->destination != NULL)
		g_object_unref (async_context->destination);

	if (async_context->message_uids != NULL) {
		g_ptr_array_foreach (
			async_context->message_uids, (GFunc) g_free, NULL);
		g_ptr_array_free (async_context->message_uids, TRUE);
	}

	if (async_context->transferred_uids != NULL) {
		g_ptr_array_foreach (
			async_context->transferred_uids, (GFunc) g_free, NULL);
		g_ptr_array_free (async_context->transferred_uids, TRUE);
	}

	if (async_context->quota_info != NULL)
		camel_folder_quota_info_free (async_context->quota_info);

	g_free (async_context->message_uid);
	g_free (async_context->start_uid);
	g_free (async_context->end_uid);

	g_slice_free (AsyncContext, async_context);
}

static void
signal_data_free (SignalData *signal_data)
{
	if (signal_data->folder != NULL)
		g_object_unref (signal_data->folder);

	g_free (signal_data->folder_name);

	g_slice_free (SignalData, signal_data);
}

static gboolean
folder_emit_changed_cb (gpointer user_data)
{
	SignalData *signal_data = user_data;
	CamelFolderChangeInfo *changes;

	camel_folder_lock (signal_data->folder, CAMEL_FOLDER_CHANGE_LOCK);
	changes = signal_data->folder->priv->pending_changes;
	signal_data->folder->priv->pending_changes = NULL;
	camel_folder_unlock (signal_data->folder, CAMEL_FOLDER_CHANGE_LOCK);

	g_signal_emit (signal_data->folder, signals[CHANGED], 0, changes);

	camel_folder_change_info_free (changes);

	return FALSE;
}

static gboolean
folder_emit_deleted_cb (gpointer user_data)
{
	SignalData *signal_data = user_data;

	g_signal_emit (signal_data->folder, signals[DELETED], 0);

	return FALSE;
}

static gboolean
folder_emit_renamed_cb (gpointer user_data)
{
	SignalData *signal_data = user_data;

	g_signal_emit (
		signal_data->folder,
		signals[RENAMED], 0,
		signal_data->folder_name);

	return FALSE;
}

static void
folder_filter_data_free (FolderFilterData *data)
{
	if (data->driver != NULL)
		g_object_unref (data->driver);
	if (data->recents != NULL)
		camel_folder_free_deep (data->folder, data->recents);
	if (data->junk != NULL)
		camel_folder_free_deep (data->folder, data->junk);
	if (data->notjunk != NULL)
		camel_folder_free_deep (data->folder, data->notjunk);

	/* XXX Too late to pass a GError here. */
	camel_folder_summary_save_to_db (data->folder->summary, NULL);

	camel_folder_thaw (data->folder);
	g_object_unref (data->folder);

	g_slice_free (FolderFilterData, data);
}

static void
folder_filter (CamelSession *session,
               GCancellable *cancellable,
               FolderFilterData *data,
               GError **error)
{
	CamelMessageInfo *info;
	CamelStore *parent_store;
	gint i, status = 0;
	CamelJunkFilter *junk_filter;
	gboolean synchronize = FALSE;
	const gchar *display_name;

	display_name = camel_folder_get_display_name (data->folder);
	parent_store = camel_folder_get_parent_store (data->folder);
	junk_filter = camel_session_get_junk_filter (session);

	/* Keep the junk filter alive until we're done. */
	if (junk_filter != NULL)
		g_object_ref (junk_filter);

	if (data->junk) {
		gboolean success = TRUE;

		/* Translators: The %s is replaced with the
		 * folder name where the operation is running. */
		camel_operation_push_message (
			cancellable, ngettext (
			"Learning new spam message in '%s'",
			"Learning new spam messages in '%s'",
			data->junk->len), display_name);

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

	if (*error != NULL)
		goto exit;

	if (data->notjunk) {
		gboolean success = TRUE;

		/* Translators: The %s is replaced with the
		 * folder name where the operation is running. */
		camel_operation_push_message (
			cancellable, ngettext (
			"Learning new ham message in '%s'",
			"Learning new ham messages in '%s'",
			data->notjunk->len), display_name);

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

	if (*error != NULL)
		goto exit;

	if (synchronize)
		camel_junk_filter_synchronize (
			junk_filter, cancellable, error);

	if (*error != NULL)
		goto exit;

	if (data->driver && data->recents) {
		CamelService *service;
		const gchar *store_uid;

		/* Translators: The %s is replaced with the
		 * folder name where the operation is running. */
		camel_operation_push_message (
			cancellable, ngettext (
			"Filtering new message in '%s'",
			"Filtering new messages in '%s'",
			data->recents->len), display_name);

		service = CAMEL_SERVICE (parent_store);
		store_uid = camel_service_get_uid (service);

		for (i = 0; status == 0 && i < data->recents->len; i++) {
			gchar *uid = data->recents->pdata[i];
			gint pc = 100 * i / data->recents->len;

			camel_operation_progress (cancellable, pc);

			info = camel_folder_get_message_info (
				data->folder, uid);
			if (info == NULL) {
				g_warning (
					"uid '%s' vanished from folder '%s'",
					uid, display_name);
				continue;
			}

			status = camel_filter_driver_filter_message (
				data->driver, NULL, info, uid, data->folder,
				store_uid, store_uid, cancellable, error);

			camel_folder_free_message_info (data->folder, info);
		}

		camel_operation_pop_message (cancellable);

		camel_filter_driver_flush (data->driver, error);
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
	GError *local_error = NULL;

	/* Default implementation. */

	msg = camel_folder_get_message_sync (source, uid, cancellable, error);
	if (!msg)
		return;

	/* if its deleted we poke the flags, so we need to copy the messageinfo */
	if ((source->folder_flags & CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY)
			&& (minfo = camel_folder_get_message_info (source, uid))) {
		info = camel_message_info_clone (minfo);
		camel_folder_free_message_info (source, minfo);
	} else
		info = camel_message_info_new_from_header (NULL, ((CamelMimePart *) msg)->headers);

	/* unset deleted flag when transferring from trash folder */
	if ((source->folder_flags & CAMEL_FOLDER_IS_TRASH) != 0)
		camel_message_info_set_flags (info, CAMEL_MESSAGE_DELETED, 0);
	/* unset junk flag when transferring from junk folder */
	if ((source->folder_flags & CAMEL_FOLDER_IS_JUNK) != 0)
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

	camel_message_info_free (info);
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
			g_value_set_string (
				value, camel_folder_get_description (
				CAMEL_FOLDER (object)));
			return;

		case PROP_DISPLAY_NAME:
			g_value_set_string (
				value, camel_folder_get_display_name (
				CAMEL_FOLDER (object)));
			return;

		case PROP_FULL_NAME:
			g_value_set_string (
				value, camel_folder_get_full_name (
				CAMEL_FOLDER (object)));
			return;

		case PROP_PARENT_STORE:
			g_value_set_object (
				value, camel_folder_get_parent_store (
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

	if (folder->priv->parent_store != NULL) {
		g_object_remove_weak_pointer (
			G_OBJECT (folder->priv->parent_store),
			&folder->priv->parent_store);
		folder->priv->parent_store = NULL;
	}

	if (folder->summary) {
		g_object_unref (folder->summary);
		folder->summary = NULL;
	}

	/* Chain up to parent's dispose () method. */
	G_OBJECT_CLASS (camel_folder_parent_class)->dispose (object);
}

static void
folder_finalize (GObject *object)
{
	CamelFolderPrivate *priv;

	priv = CAMEL_FOLDER_GET_PRIVATE (object);

	g_free (priv->full_name);
	g_free (priv->display_name);
	g_free (priv->description);

	camel_folder_change_info_free (priv->changed_frozen);

	if (priv->pending_changes != NULL)
		camel_folder_change_info_free (priv->pending_changes);

	g_static_rec_mutex_free (&priv->lock);
	g_static_mutex_free (&priv->change_lock);

	/* Chain up to parent's finalize () method. */
	G_OBJECT_CLASS (camel_folder_parent_class)->finalize (object);
}

static gint
folder_get_message_count (CamelFolder *folder)
{
	g_return_val_if_fail (folder->summary != NULL, -1);

	return camel_folder_summary_count (folder->summary);
}

static CamelMessageFlags
folder_get_permanent_flags (CamelFolder *folder)
{
	return folder->permanent_flags;
}

static CamelMessageFlags
folder_get_message_flags (CamelFolder *folder,
                          const gchar *uid)
{
	CamelMessageInfo *info;
	CamelMessageFlags flags;

	g_return_val_if_fail (folder->summary != NULL, 0);

	info = camel_folder_summary_get (folder->summary, uid);
	if (info == NULL)
		return 0;

	flags = camel_message_info_flags (info);
	camel_message_info_free (info);

	return flags;
}

static gboolean
folder_set_message_flags (CamelFolder *folder,
                          const gchar *uid,
                          CamelMessageFlags flags,
                          CamelMessageFlags set)
{
	CamelMessageInfo *info;
	gint res;

	g_return_val_if_fail (folder->summary != NULL, FALSE);

	info = camel_folder_summary_get (folder->summary, uid);
	if (info == NULL)
		return FALSE;

	res = camel_message_info_set_flags (info, flags, set);
	camel_message_info_free (info);

	return res;
}

static gboolean
folder_get_message_user_flag (CamelFolder *folder,
                              const gchar *uid,
                              const gchar *name)
{
	CamelMessageInfo *info;
	gboolean ret;

	g_return_val_if_fail (folder->summary != NULL, FALSE);

	info = camel_folder_summary_get (folder->summary, uid);
	if (info == NULL)
		return FALSE;

	ret = camel_message_info_user_flag (info, name);
	camel_message_info_free (info);

	return ret;
}

static void
folder_set_message_user_flag (CamelFolder *folder,
                              const gchar *uid,
                              const gchar *name,
                              gboolean value)
{
	CamelMessageInfo *info;

	g_return_if_fail (folder->summary != NULL);

	info = camel_folder_summary_get (folder->summary, uid);
	if (info == NULL)
		return;

	camel_message_info_set_user_flag (info, name, value);
	camel_message_info_free (info);
}

static const gchar *
folder_get_message_user_tag (CamelFolder *folder,
                             const gchar *uid,
                             const gchar *name)
{
	CamelMessageInfo *info;
	const gchar *ret;

	g_return_val_if_fail (folder->summary != NULL, NULL);

	info = camel_folder_summary_get (folder->summary, uid);
	if (info == NULL)
		return NULL;

	ret = camel_message_info_user_tag (info, name);
	camel_message_info_free (info);

	return ret;
}

static void
folder_set_message_user_tag (CamelFolder *folder,
                             const gchar *uid,
                             const gchar *name,
                             const gchar *value)
{
	CamelMessageInfo *info;

	g_return_if_fail (folder->summary != NULL);

	info = camel_folder_summary_get (folder->summary, uid);
	if (info == NULL)
		return;

	camel_message_info_set_user_tag (info, name, value);
	camel_message_info_free (info);
}

static GPtrArray *
folder_get_uids (CamelFolder *folder)
{
	g_return_val_if_fail (folder->summary != NULL, NULL);

	return camel_folder_summary_get_array (folder->summary);
}

static GPtrArray *
folder_get_uncached_uids (CamelFolder *folder,
                          GPtrArray *uids,
                          GError **error)
{
	GPtrArray *result;
	gint i;

	result = g_ptr_array_new ();

	g_ptr_array_set_size (result, uids->len);
	for (i = 0; i < uids->len; i++)
		result->pdata[i] =
			(gpointer) camel_pstring_strdup (uids->pdata[i]);

	return result;
}

static void
folder_free_uids (CamelFolder *folder,
                  GPtrArray *array)
{
	camel_folder_summary_free_array (array);
}

static gint
folder_cmp_uids (CamelFolder *folder,
                 const gchar *uid1,
                 const gchar *uid2)
{
	g_return_val_if_fail (uid1 != NULL, 0);
	g_return_val_if_fail (uid2 != NULL, 0);

	return strtoul (uid1, NULL, 10) - strtoul (uid2, NULL, 10);
}

static void
folder_sort_uids (CamelFolder *folder,
                  GPtrArray *uids)
{
	g_qsort_with_data (
		uids->pdata, uids->len,
		sizeof (gpointer), cmp_array_uids, folder);
}

static GPtrArray *
folder_get_summary (CamelFolder *folder)
{
	g_return_val_if_fail (folder->summary != NULL, NULL);

	return camel_folder_summary_get_array (folder->summary);
}

static void
folder_free_summary (CamelFolder *folder,
                     GPtrArray *array)
{
	camel_folder_summary_free_array (array);
}

static void
folder_search_free (CamelFolder *folder,
                    GPtrArray *result)
{
	gint i;

	for (i = 0; i < result->len; i++)
		camel_pstring_free (g_ptr_array_index (result, i));
	g_ptr_array_free (result, TRUE);
}

static CamelMessageInfo *
folder_get_message_info (CamelFolder *folder,
                         const gchar *uid)
{
	g_return_val_if_fail (folder->summary != NULL, NULL);

	return camel_folder_summary_get (folder->summary, uid);
}

static void
folder_ref_message_info (CamelFolder *folder,
                         CamelMessageInfo *info)
{
	g_return_if_fail (folder->summary != NULL);

	camel_message_info_ref (info);
}

static void
folder_free_message_info (CamelFolder *folder,
                          CamelMessageInfo *info)
{
	g_return_if_fail (folder->summary != NULL);

	camel_message_info_free (info);
}

static void
folder_delete (CamelFolder *folder)
{
	if (folder->summary)
		camel_folder_summary_clear (folder->summary, NULL);
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

	camel_folder_lock (folder, CAMEL_FOLDER_CHANGE_LOCK);

	folder->priv->frozen++;
	if (folder->summary)
		g_object_freeze_notify (G_OBJECT (folder->summary));

	d (printf ("freeze (%p '%s') = %d\n", folder, folder->full_name, folder->priv->frozen));
	camel_folder_unlock (folder, CAMEL_FOLDER_CHANGE_LOCK);
}

static void
folder_thaw (CamelFolder *folder)
{
	CamelFolderChangeInfo *info = NULL;

	g_return_if_fail (folder->priv->frozen > 0);

	camel_folder_lock (folder, CAMEL_FOLDER_CHANGE_LOCK);

	folder->priv->frozen--;
	if (folder->summary)
		g_object_thaw_notify (G_OBJECT (folder->summary));

	d (printf ("thaw (%p '%s') = %d\n", folder, folder->full_name, folder->priv->frozen));

	if (folder->priv->frozen == 0
	    && camel_folder_change_info_changed (folder->priv->changed_frozen)) {
		info = folder->priv->changed_frozen;
		folder->priv->changed_frozen = camel_folder_change_info_new ();
	}

	camel_folder_unlock (folder, CAMEL_FOLDER_CHANGE_LOCK);

	if (info) {
		camel_folder_changed (folder, info);
		camel_folder_change_info_free (info);
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
		*transferred_uids = g_ptr_array_new ();
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

static void
folder_append_message_thread (GSimpleAsyncResult *simple,
                              GObject *object,
                              GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_folder_append_message_sync (
		CAMEL_FOLDER (object), async_context->message,
		async_context->info, &async_context->message_uid,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
folder_append_message (CamelFolder *folder,
                       CamelMimeMessage *message,
                       CamelMessageInfo *info,
                       gint io_priority,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->message = g_object_ref (message);
	async_context->info = camel_message_info_ref (info);

	simple = g_simple_async_result_new (
		G_OBJECT (folder), callback,
		user_data, folder_append_message);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, folder_append_message_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
folder_append_message_finish (CamelFolder *folder,
                              GAsyncResult *result,
                              gchar **appended_uid,
                              GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (folder), folder_append_message), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (appended_uid != NULL) {
		*appended_uid = async_context->message_uid;
		async_context->message_uid = NULL;
	}

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
folder_expunge_thread (GSimpleAsyncResult *simple,
                       GObject *object,
                       GCancellable *cancellable)
{
	GError *error = NULL;

	camel_folder_expunge_sync (
		CAMEL_FOLDER (object), cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
folder_expunge (CamelFolder *folder,
                gint io_priority,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (folder), callback, user_data, folder_expunge);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, folder_expunge_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
folder_expunge_finish (CamelFolder *folder,
                       GAsyncResult *result,
                       GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (folder), folder_expunge), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
fetch_messages_thread (GSimpleAsyncResult *simple,
                       GObject *object,
                       GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->success = camel_folder_fetch_messages_sync (
		CAMEL_FOLDER (object), async_context->fetch_type, async_context->limit, cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
fetch_messages (CamelFolder *folder,
                CamelFetchType type,
                gint limit,
                gint io_priority,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->fetch_type = type;
	async_context->limit = limit;

	simple = g_simple_async_result_new (
		G_OBJECT (folder), callback, user_data, fetch_messages);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, fetch_messages_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
fetch_messages_finish (CamelFolder *folder,
                       GAsyncResult *result,
                       GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (folder), fetch_messages), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error)
		&& async_context->success;
}

static void
folder_get_message_thread (GSimpleAsyncResult *simple,
                           GObject *object,
                           GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->message = camel_folder_get_message_sync (
		CAMEL_FOLDER (object), async_context->message_uid,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
folder_get_message (CamelFolder *folder,
                    const gchar *message_uid,
                    gint io_priority,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->message_uid = g_strdup (message_uid);

	simple = g_simple_async_result_new (
		G_OBJECT (folder), callback, user_data, folder_get_message);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, folder_get_message_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static CamelMimeMessage *
folder_get_message_finish (CamelFolder *folder,
                           GAsyncResult *result,
                           GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (folder), folder_get_message), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_object_ref (async_context->message);
}

static void
folder_get_quota_info_thread (GSimpleAsyncResult *simple,
                              GObject *object,
                              GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->quota_info = camel_folder_get_quota_info_sync (
		CAMEL_FOLDER (object), cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static CamelFolderQuotaInfo *
folder_get_quota_info_sync (CamelFolder *folder,
                            GCancellable *cancellable,
                            GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Quota information not supported for folder '%s'"),
		camel_folder_get_display_name (folder));

	return NULL;
}

static void
folder_get_quota_info (CamelFolder *folder,
                       gint io_priority,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);

	simple = g_simple_async_result_new (
		G_OBJECT (folder), callback,
		user_data, folder_get_quota_info);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, folder_get_quota_info_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static CamelFolderQuotaInfo *
folder_get_quota_info_finish (CamelFolder *folder,
                              GAsyncResult *result,
                              GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	CamelFolderQuotaInfo *quota_info;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (folder), folder_get_quota_info), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	quota_info = async_context->quota_info;
	async_context->quota_info = NULL;

	return quota_info;
}

static void
purge_message_cache_thread (GSimpleAsyncResult *simple,
                       GObject *object,
                       GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->success = camel_folder_purge_message_cache_sync (
		CAMEL_FOLDER (object), async_context->start_uid, async_context->end_uid, cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
purge_message_cache (CamelFolder *folder,
                     gchar *start_uid,
                     gchar *end_uid,
                     gint io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->start_uid = g_strdup (start_uid);
	async_context->end_uid = g_strdup (end_uid);

	simple = g_simple_async_result_new (
		G_OBJECT (folder), callback, user_data, purge_message_cache);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, purge_message_cache_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
purge_message_cache_finish (CamelFolder *folder,
                            GAsyncResult *result,
                            GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (folder), purge_message_cache), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error)
		&& async_context->success;
}

static void
folder_refresh_info_thread (GSimpleAsyncResult *simple,
                            GObject *object,
                            GCancellable *cancellable)
{
	GError *error = NULL;

	camel_folder_refresh_info_sync (
		CAMEL_FOLDER (object), cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
folder_refresh_info (CamelFolder *folder,
                     gint io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (folder), callback, user_data, folder_refresh_info);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, folder_refresh_info_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
folder_refresh_info_finish (CamelFolder *folder,
                            GAsyncResult *result,
                            GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (folder), folder_refresh_info), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
folder_synchronize_thread (GSimpleAsyncResult *simple,
                           GObject *object,
                           GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_folder_synchronize_sync (
		CAMEL_FOLDER (object), async_context->expunge,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
folder_synchronize (CamelFolder *folder,
                    gboolean expunge,
                    gint io_priority,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->expunge = expunge;

	simple = g_simple_async_result_new (
		G_OBJECT (folder), callback, user_data, folder_synchronize);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, folder_synchronize_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
folder_synchronize_finish (CamelFolder *folder,
                           GAsyncResult *result,
                           GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (folder), folder_synchronize), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
folder_synchronize_message_thread (GSimpleAsyncResult *simple,
                                   GObject *object,
                                   GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_folder_synchronize_message_sync (
		CAMEL_FOLDER (object), async_context->message_uid,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
folder_synchronize_message (CamelFolder *folder,
                            const gchar *message_uid,
                            gint io_priority,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->message_uid = g_strdup (message_uid);

	simple = g_simple_async_result_new (
		G_OBJECT (folder), callback,
		user_data, folder_synchronize_message);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, folder_synchronize_message_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
folder_synchronize_message_finish (CamelFolder *folder,
                                   GAsyncResult *result,
                                   GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (folder),
		folder_synchronize_message), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
folder_transfer_messages_to_thread (GSimpleAsyncResult *simple,
                                    GObject *object,
                                    GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_folder_transfer_messages_to_sync (
		CAMEL_FOLDER (object), async_context->message_uids,
		async_context->destination, async_context->delete_originals,
		&async_context->transferred_uids, cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
folder_transfer_messages_to (CamelFolder *source,
                             GPtrArray *message_uids,
                             CamelFolder *destination,
                             gboolean delete_originals,
                             gint io_priority,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	guint ii;

	async_context = g_slice_new0 (AsyncContext);
	async_context->message_uids = g_ptr_array_new ();
	async_context->destination = g_object_ref (destination);
	async_context->delete_originals = delete_originals;

	for (ii = 0; ii < message_uids->len; ii++)
		g_ptr_array_add (
			async_context->message_uids,
			g_strdup (message_uids->pdata[ii]));

	simple = g_simple_async_result_new (
		G_OBJECT (source), callback,
		user_data, folder_transfer_messages_to);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, folder_transfer_messages_to_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
folder_transfer_messages_to_finish (CamelFolder *source,
                                    GAsyncResult *result,
                                    GPtrArray **transferred_uids,
                                    GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (source),
		folder_transfer_messages_to), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (transferred_uids != NULL) {
		*transferred_uids = async_context->transferred_uids;
		async_context->transferred_uids = NULL;
	}

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
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

	parent_store = camel_folder_get_parent_store (folder);
	session = camel_service_get_session (CAMEL_SERVICE (parent_store));
	junk_filter = camel_session_get_junk_filter (session);

	camel_folder_lock (folder, CAMEL_FOLDER_CHANGE_LOCK);
	if (folder->priv->frozen) {
		camel_folder_change_info_cat (folder->priv->changed_frozen, info);
		camel_folder_unlock (folder, CAMEL_FOLDER_CHANGE_LOCK);
		g_signal_stop_emission (folder, signals[CHANGED], 0);
		return;
	}
	camel_folder_unlock (folder, CAMEL_FOLDER_CHANGE_LOCK);

	if (junk_filter != NULL && info->uid_changed->len) {
		CamelMessageFlags flags;

		for (i = 0; i < info->uid_changed->len; i++) {
			flags = camel_folder_get_message_flags (folder, info->uid_changed->pdata[i]);
			if (flags & CAMEL_MESSAGE_JUNK_LEARN) {
				if (flags & CAMEL_MESSAGE_JUNK) {
					if (!junk)
						junk = g_ptr_array_new ();
					g_ptr_array_add (junk, g_strdup (info->uid_changed->pdata[i]));
				} else {
					if (!notjunk)
						notjunk = g_ptr_array_new ();
					g_ptr_array_add (notjunk, g_strdup (info->uid_changed->pdata[i]));
				}
				/* reset junk learn flag so that we don't process it again*/
				camel_folder_set_message_flags (
					folder, info->uid_changed->pdata[i],
					CAMEL_MESSAGE_JUNK_LEARN, 0);
			}
		}
	}

	if ((folder->folder_flags & (CAMEL_FOLDER_FILTER_RECENT | CAMEL_FOLDER_FILTER_JUNK))
	    && p->uid_filter->len > 0)
		driver = camel_session_get_filter_driver (
			session,
			(folder->folder_flags & CAMEL_FOLDER_FILTER_RECENT)
			? "incoming" : "junktest", NULL);

	if (driver) {
		recents = g_ptr_array_new ();
		for (i = 0; i < p->uid_filter->len; i++)
			g_ptr_array_add (recents, g_strdup (p->uid_filter->pdata[i]));

		g_ptr_array_set_size (p->uid_filter, 0);
	}

	if (driver || junk || notjunk) {
		FolderFilterData *data;

		data = g_slice_new0 (FolderFilterData);
		data->recents = recents;
		data->junk = junk;
		data->notjunk = notjunk;
		data->folder = g_object_ref (folder);
		data->driver = driver;

		camel_folder_freeze (folder);

		/* Copy changes back to changed_frozen list to retain
		 * them while we are filtering */
		camel_folder_lock (folder, CAMEL_FOLDER_CHANGE_LOCK);
		camel_folder_change_info_cat (
			folder->priv->changed_frozen, info);
		camel_folder_unlock (folder, CAMEL_FOLDER_CHANGE_LOCK);

		camel_session_submit_job (
			session, (CamelSessionCallback) folder_filter,
			data, (GDestroyNotify) folder_filter_data_free);

		g_signal_stop_emission (folder, signals[CHANGED], 0);
	}
}

static void
camel_folder_class_init (CamelFolderClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = folder_set_property;
	object_class->get_property = folder_get_property;
	object_class->dispose = folder_dispose;
	object_class->finalize = folder_finalize;

	class->get_message_count = folder_get_message_count;
	class->get_permanent_flags = folder_get_permanent_flags;
	class->get_message_flags = folder_get_message_flags;
	class->set_message_flags = folder_set_message_flags;
	class->get_message_user_flag = folder_get_message_user_flag;
	class->set_message_user_flag = folder_set_message_user_flag;
	class->get_message_user_tag = folder_get_message_user_tag;
	class->set_message_user_tag = folder_set_message_user_tag;
	class->get_uids = folder_get_uids;
	class->get_uncached_uids = folder_get_uncached_uids;
	class->free_uids = folder_free_uids;
	class->cmp_uids = folder_cmp_uids;
	class->sort_uids = folder_sort_uids;
	class->get_summary = folder_get_summary;
	class->free_summary = folder_free_summary;
	class->search_free = folder_search_free;
	class->get_message_info = folder_get_message_info;
	class->ref_message_info = folder_ref_message_info;
	class->free_message_info = folder_free_message_info;
	class->delete_ = folder_delete;
	class->rename = folder_rename;
	class->freeze = folder_freeze;
	class->thaw = folder_thaw;
	class->is_frozen = folder_is_frozen;
	class->get_quota_info_sync = folder_get_quota_info_sync;
	class->refresh_info_sync = folder_refresh_info_sync;
	class->transfer_messages_to_sync = folder_transfer_messages_to_sync;
	class->changed = folder_changed;

	class->append_message = folder_append_message;
	class->append_message_finish = folder_append_message_finish;
	class->expunge = folder_expunge;
	class->expunge_finish = folder_expunge_finish;
	class->fetch_messages= fetch_messages;
	class->fetch_messages_finish = fetch_messages_finish;
	class->get_message = folder_get_message;
	class->get_message_finish = folder_get_message_finish;
	class->get_quota_info = folder_get_quota_info;
	class->get_quota_info_finish = folder_get_quota_info_finish;
	class->purge_message_cache= purge_message_cache;
	class->purge_message_cache_finish = purge_message_cache_finish;
	class->refresh_info = folder_refresh_info;
	class->refresh_info_finish = folder_refresh_info_finish;
	class->synchronize = folder_synchronize;
	class->synchronize_finish = folder_synchronize_finish;
	class->synchronize_message = folder_synchronize_message;
	class->synchronize_message_finish = folder_synchronize_message_finish;
	class->transfer_messages_to = folder_transfer_messages_to;
	class->transfer_messages_to_finish = folder_transfer_messages_to_finish;

	/**
	 * CamelFolder:description
	 *
	 * The folder's description.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_DESCRIPTION,
		g_param_spec_string (
			"description",
			"Description",
			"The folder's description",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	/**
	 * CamelFolder:display-name
	 *
	 * The folder's display name.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_DISPLAY_NAME,
		g_param_spec_string (
			"display-name",
			"Display Name",
			"The folder's display name",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	/**
	 * CamelFolder:full-name
	 *
	 * The folder's fully qualified name.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_FULL_NAME,
		g_param_spec_string (
			"full-name",
			"Full Name",
			"The folder's fully qualified name",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT));

	/**
	 * CamelFolder:parent-store
	 *
	 * The #CamelStore to which the folder belongs.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_PARENT_STORE,
		g_param_spec_object (
			"parent-store",
			"Parent Store",
			"The store to which the folder belongs",
			CAMEL_TYPE_STORE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));

	/**
	 * CamelFolder::changed
	 * @folder: the #CamelFolder which emitted the signal
	 **/
	signals[CHANGED] = g_signal_new (
		"changed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelFolderClass, changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	/**
	 * CamelFolder::deleted
	 * @folder: the #CamelFolder which emitted the signal
	 **/
	signals[DELETED] = g_signal_new (
		"deleted",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelFolderClass, deleted),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
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
		NULL, NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE, 1,
		G_TYPE_STRING);
}

static void
camel_folder_init (CamelFolder *folder)
{
	folder->priv = CAMEL_FOLDER_GET_PRIVATE (folder);
	folder->priv->frozen = 0;
	folder->priv->changed_frozen = camel_folder_change_info_new ();

	g_static_rec_mutex_init (&folder->priv->lock);
	g_static_mutex_init (&folder->priv->change_lock);
}

GQuark
camel_folder_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "camel-folder-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

/**
 * camel_folder_set_lock_async:
 * @folder: a #CamelFolder
 * @skip_folder_lock:
 *
 * FIXME Document me!
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

	if (g_strcmp0 (folder->priv->full_name, full_name) == 0)
		return;

	g_free (folder->priv->full_name);
	folder->priv->full_name = g_strdup (full_name);

	g_object_notify (G_OBJECT (folder), "full-name");
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

	if (g_strcmp0 (folder->priv->display_name, display_name) == 0)
		return;

	g_free (folder->priv->display_name);
	folder->priv->display_name = g_strdup (display_name);

	g_object_notify (G_OBJECT (folder), "display-name");
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

	if (g_strcmp0 (folder->priv->description, description) == 0)
		return;

	g_free (folder->priv->description);
	folder->priv->description = g_strdup (description);

	g_object_notify (G_OBJECT (folder), "description");
}

/**
 * camel_folder_get_parent_store:
 * @folder: a #CamelFolder
 *
 * Returns: the parent #CamelStore of the folder
 **/
CamelStore *
camel_folder_get_parent_store (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	return CAMEL_STORE (folder->priv->parent_store);
}

/**
 * camel_folder_get_message_count:
 * @folder: a #CamelFolder
 *
 * Returns: the number of messages in the folder, or %-1 if unknown
 **/
gint
camel_folder_get_message_count (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_count != NULL, -1);

	return class->get_message_count (folder);
}

/**
 * camel_folder_get_unread_message_count:
 * @folder: a #CamelFolder
 *
 * DEPRECATED: use camel_object_get() instead.
 *
 * Returns: the number of unread messages in the folder, or %-1 if
 * unknown
 **/
gint
camel_folder_get_unread_message_count (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);
	g_return_val_if_fail (folder->summary != NULL, -1);

	return camel_folder_summary_get_unread_count (folder->summary);
}

/**
 * camel_folder_get_deleted_message_count:
 * @folder: a #CamelFolder
 *
 * Returns: the number of deleted messages in the folder, or %-1 if
 * unknown
 **/
gint
camel_folder_get_deleted_message_count (CamelFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), -1);
	g_return_val_if_fail (folder->summary != NULL, -1);

	return camel_folder_summary_get_deleted_count (folder->summary);
}

/**
 * camel_folder_get_permanent_flags:
 * @folder: a #CamelFolder
 *
 * Returns: the set of #CamelMessageFlags that can be permanently
 * stored on a message between sessions. If it includes
 * #CAMEL_FLAG_USER, then user-defined flags will be remembered.
 **/
CamelMessageFlags
camel_folder_get_permanent_flags (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_permanent_flags != NULL, 0);

	return class->get_permanent_flags (folder);
}

/**
 * camel_folder_get_message_flags:
 * @folder: a #CamelFolder
 * @uid: the UID of a message in @folder
 *
 * Deprecated: Use camel_folder_get_message_info() instead.
 *
 * Returns: the #CamelMessageFlags that are set on the indicated
 * message.
 **/
CamelMessageFlags
camel_folder_get_message_flags (CamelFolder *folder,
                                const gchar *uid)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);
	g_return_val_if_fail (uid != NULL, 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_flags != NULL, 0);

	return class->get_message_flags (folder, uid);
}

/**
 * camel_folder_set_message_flags:
 * @folder: a #CamelFolder
 * @uid: the UID of a message in @folder
 * @flags: a set of #CamelMessageFlag values to set
 * @set: the mask of values in @flags to use.
 *
 * Sets those flags specified by @flags to the values specified by @set
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See camel_folder_get_permanent_flags())
 *
 * E.g. to set the deleted flag and clear the draft flag, use
 * camel_folder_set_message_flags (folder, uid, CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_DRAFT, CAMEL_MESSAGE_DELETED);
 *
 * DEPRECATED: Use camel_message_info_set_flags() on the message info directly
 * (when it works)
 *
 * Returns: %TRUE if the flags were changed or %FALSE otherwise
 **/
gboolean
camel_folder_set_message_flags (CamelFolder *folder,
                                const gchar *uid,
                                CamelMessageFlags flags,
                                CamelMessageFlags set)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->set_message_flags != NULL, FALSE);

	if ((flags & (CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_JUNK_LEARN)) == CAMEL_MESSAGE_JUNK) {
		flags |= CAMEL_MESSAGE_JUNK_LEARN;
		set &= ~CAMEL_MESSAGE_JUNK_LEARN;
	}

	return class->set_message_flags (folder, uid, flags, set);
}

/**
 * camel_folder_get_message_user_flag:
 * @folder: a #CamelFolder
 * @uid: the UID of a message in @folder
 * @name: the name of a user flag
 *
 * DEPRECATED: Use camel_message_info_get_user_flag() on the message
 * info directly
 *
 * Returns: %TRUE if the given user flag is set on the message or
 * %FALSE otherwise
 **/
gboolean
camel_folder_get_message_user_flag (CamelFolder *folder,
                                    const gchar *uid,
                                    const gchar *name)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);
	g_return_val_if_fail (uid != NULL, 0);
	g_return_val_if_fail (name != NULL, 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_user_flag != NULL, 0);

	return class->get_message_user_flag (folder, uid, name);
}

/**
 * camel_folder_set_message_user_flag:
 * @folder: a #CamelFolder
 * @uid: the UID of a message in @folder
 * @name: the name of the user flag to set
 * @value: the value to set it to
 *
 * DEPRECATED: Use camel_message_info_set_user_flag() on the
 * #CamelMessageInfo directly (when it works)
 *
 * Sets the user flag specified by @name to the value specified by @value
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See camel_folder_get_permanent_flags())
 **/
void
camel_folder_set_message_user_flag (CamelFolder *folder,
                                    const gchar *uid,
                                    const gchar *name,
                                    gboolean value)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (name != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->set_message_user_flag != NULL);

	class->set_message_user_flag (folder, uid, name, value);
}

/**
 * camel_folder_get_message_user_tag:
 * @folder: a #CamelFolder
 * @uid: the UID of a message in @folder
 * @name: the name of a user tag
 *
 * DEPRECATED: Use camel_message_info_get_user_tag() on the
 * #CamelMessageInfo directly.
 *
 * Returns: the value of the user tag
 **/
const gchar *
camel_folder_get_message_user_tag (CamelFolder *folder,
                                   const gchar *uid,
                                   const gchar *name)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_user_tag != NULL, NULL);

	/* FIXME: should duplicate string */
	return class->get_message_user_tag (folder, uid, name);
}

/**
 * camel_folder_set_message_user_tag:
 * @folder: a #CamelFolder
 * @uid: the UID of a message in @folder
 * @name: the name of the user tag to set
 * @value: the value to set it to
 *
 * DEPRECATED: Use camel_message_info_set_user_tag() on the
 * #CamelMessageInfo directly (when it works).
 *
 * Sets the user tag specified by @name to the value specified by @value
 * on the indicated message. (This may or may not persist after the
 * folder or store is closed. See camel_folder_get_permanent_flags())
 **/
void
camel_folder_set_message_user_tag (CamelFolder *folder,
                                   const gchar *uid,
                                   const gchar *name,
                                   const gchar *value)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (name != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->set_message_user_tag != NULL);

	class->set_message_user_tag (folder, uid, name, value);
}

/**
 * camel_folder_get_message_info:
 * @folder: a #CamelFolder
 * @uid: the uid of a message
 *
 * Retrieve the #CamelMessageInfo for the specified @uid.  This return
 * must be freed using camel_folder_free_message_info().
 *
 * Returns: the summary information for the indicated message, or %NULL
 * if the uid does not exist
 **/
CamelMessageInfo *
camel_folder_get_message_info (CamelFolder *folder,
                               const gchar *uid)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_info != NULL, NULL);

	return class->get_message_info (folder, uid);
}

/**
 * camel_folder_free_message_info:
 * @folder: a #CamelFolder
 * @info: a #CamelMessageInfo
 *
 * Free (unref) a #CamelMessageInfo, previously obtained with
 * camel_folder_get_message_info().
 **/
void
camel_folder_free_message_info (CamelFolder *folder,
                                CamelMessageInfo *info)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (info != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->free_message_info != NULL);

	class->free_message_info (folder, info);
}

/**
 * camel_folder_ref_message_info:
 * @folder: a #CamelFolder
 * @info: a #CamelMessageInfo
 *
 * DEPRECATED: Use camel_message_info_ref() directly.
 *
 * Ref a #CamelMessageInfo, previously obtained with
 * camel_folder_get_message_info().
 **/
void
camel_folder_ref_message_info (CamelFolder *folder,
                               CamelMessageInfo *info)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (info != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->ref_message_info != NULL);

	class->ref_message_info (folder, info);
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

	return folder->folder_flags & CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY;
}

/* UIDs stuff */

/**
 * camel_folder_get_uids:
 * @folder: a #CamelFolder
 *
 * Get the list of UIDs available in a folder. This routine is useful
 * for finding what messages are available when the folder does not
 * support summaries. The returned array should not be modified, and
 * must be freed by passing it to camel_folder_free_uids().
 *
 * Returns: a GPtrArray of UIDs corresponding to the messages available
 * in the folder
 **/
GPtrArray *
camel_folder_get_uids (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_uids != NULL, NULL);

	return class->get_uids (folder);
}

/**
 * camel_folder_free_uids:
 * @folder: a #CamelFolder
 * @array: the array of uids to free
 *
 * Frees the array of UIDs returned by camel_folder_get_uids().
 **/
void
camel_folder_free_uids (CamelFolder *folder,
                        GPtrArray *array)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (array != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->free_uids != NULL);

	class->free_uids (folder, array);
}

/**
 * camel_folder_get_uncached_uids:
 * @folder: a #CamelFolder
 * @uids: the array of uids to filter down to uncached ones.
 *
 * Returns the known-uncached uids from a list of uids. It may return uids
 * which are locally cached but should never filter out a uid which is not
 * locally cached. Free the result by called camel_folder_free_uids().
 * Frees the array of UIDs returned by camel_folder_get_uids().
 *
 * Since: 2.26
 **/
GPtrArray *
camel_folder_get_uncached_uids (CamelFolder *folder,
                                GPtrArray *uids,
                                GError **error)
{
	CamelFolderClass *class;
	GPtrArray *uncached_uids;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uids != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_uncached_uids != NULL, NULL);

	uncached_uids = class->get_uncached_uids (folder, uids, error);
	CAMEL_CHECK_GERROR (folder, get_uncached_uids, uncached_uids != NULL, error);

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
	g_return_val_if_fail (class->cmp_uids != NULL, 0);

	return class->cmp_uids (folder, uid1, uid2);
}

/**
 * camel_folder_sort_uids:
 * @folder: a #CamelFolder
 * @uids: array of uids
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
	g_return_if_fail (class->sort_uids != NULL);

	class->sort_uids (folder, uids);
}

/**
 * camel_folder_get_summary:
 * @folder: a #CamelFolder
 *
 * This returns the summary information for the folder. This array
 * should not be modified, and must be freed with
 * camel_folder_free_summary().
 *
 * Returns: an array of #CamelMessageInfo
 **/
GPtrArray *
camel_folder_get_summary (CamelFolder *folder)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_summary != NULL, NULL);

	return class->get_summary (folder);
}

/**
 * camel_folder_free_summary:
 * @folder: a #CamelFolder
 * @array: the summary array to free
 *
 * Frees the summary array returned by camel_folder_get_summary().
 **/
void
camel_folder_free_summary (CamelFolder *folder,
                           GPtrArray *array)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (array != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->free_summary != NULL);

	class->free_summary (folder, array);
}

/**
 * camel_folder_search_by_expression:
 * @folder: a #CamelFolder
 * @expr: a search expression
 * @cancellable: a #GCancellable
 * @error: return location for a #GError, or %NULL
 *
 * Searches the folder for messages matching the given search expression.
 *
 * Returns: a #GPtrArray of uids of matching messages. The caller must
 * free the list and each of the elements when it is done.
 **/
GPtrArray *
camel_folder_search_by_expression (CamelFolder *folder,
                                   const gchar *expression,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelFolderClass *class;
	GPtrArray *matches;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->search_by_expression != NULL, NULL);

	/* NOTE: that it is upto the callee to CAMEL_FOLDER_REC_LOCK */

	matches = class->search_by_expression (folder, expression, cancellable, error);
	CAMEL_CHECK_GERROR (folder, search_by_expression, matches != NULL, error);

	return matches;
}

/**
 * camel_folder_count_by_expression:
 * @folder: a #CamelFolder
 * @expression: a search expression
 * @cancellable: a #GCancellable
 * @error: return location for a #GError, or %NULL
 *
 * Searches the folder for count of messages matching the given search expression.
 *
 * Returns: an interger
 *
 * Since: 2.26
 **/
guint32
camel_folder_count_by_expression (CamelFolder *folder,
                                  const gchar *expression,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), 0);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->count_by_expression != NULL, 0);

	/* NOTE: that it is upto the callee to CAMEL_FOLDER_REC_LOCK */

	return class->count_by_expression (folder, expression, cancellable, error);
}

/**
 * camel_folder_search_by_uids:
 * @folder: a #CamelFolder
 * @expr: search expression
 * @uids: array of uid's to match against.
 * @cancellable: a #GCancellable
 * @error: return location for a #GError, or %NULL
 *
 * Search a subset of uid's for an expression match.
 *
 * Returns: a #GPtrArray of uids of matching messages. The caller must
 * free the list and each of the elements when it is done.
 **/
GPtrArray *
camel_folder_search_by_uids (CamelFolder *folder,
                             const gchar *expr,
                             GPtrArray *uids,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelFolderClass *class;
	GPtrArray *matches;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->search_by_uids != NULL, NULL);

	/* NOTE: that it is upto the callee to CAMEL_FOLDER_REC_LOCK */

	matches = class->search_by_uids (folder, expr, uids, cancellable, error);
	CAMEL_CHECK_GERROR (folder, search_by_uids, matches != NULL, error);

	return matches;
}

/**
 * camel_folder_search_free:
 * @folder: a #CamelFolder
 * @result: search results to free
 *
 * Free the result of a search as gotten by camel_folder_search() or
 * camel_folder_search_by_uids().
 **/
void
camel_folder_search_free (CamelFolder *folder,
                          GPtrArray *result)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (result != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->search_free != NULL);

	/* NOTE: upto the callee to CAMEL_FOLDER_REC_LOCK */

	class->search_free (folder, result);
}

/**
 * camel_folder_delete:
 * @folder: a #CamelFolder
 *
 * Marks @folder as deleted and performs any required cleanup.
 *
 * This also emits the #CamelFolder::deleted signal from an idle source on
 * the main loop.  The idle source's priority is #G_PRIORITY_DEFAULT_IDLE.
 **/
void
camel_folder_delete (CamelFolder *folder)
{
	CamelFolderClass *class;
	CamelStore *parent_store;
	CamelService *service;
	CamelSession *session;
	SignalData *signal_data;
	const gchar *full_name;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->delete_ != NULL);

	camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);
	if (folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED) {
		camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
		return;
	}

	folder->folder_flags |= CAMEL_FOLDER_HAS_BEEN_DELETED;

	class->delete_ (folder);

	camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);

	/* Delete the references of the folder from the DB.*/
	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	camel_db_delete_folder (parent_store->cdb_w, full_name, NULL);

	service = CAMEL_SERVICE (parent_store);
	session = camel_service_get_session (service);

	signal_data = g_slice_new0 (SignalData);
	signal_data->folder = g_object_ref (folder);

	camel_session_idle_add (
		session, G_PRIORITY_DEFAULT_IDLE,
		folder_emit_deleted_cb,
		signal_data, (GDestroyNotify) signal_data_free);
}

/**
 * camel_folder_rename:
 * @folder: a #CamelFolder
 * @new_name: new name for the folder
 *
 * Marks @folder as renamed.
 *
 * This also emits the #CamelFolder::renamed signal from an idle source on
 * the main loop.  The idle source's priority is #G_PRIORITY_DEFAULT_IDLE.
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
	SignalData *signal_data;
	gchar *old_name;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (new_name != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->rename != NULL);

	old_name = g_strdup (camel_folder_get_full_name (folder));

	class->rename (folder, new_name);

	parent_store = camel_folder_get_parent_store (folder);
	camel_db_rename_folder (parent_store->cdb_w, old_name, new_name, NULL);

	service = CAMEL_SERVICE (parent_store);
	session = camel_service_get_session (service);

	signal_data = g_slice_new0 (SignalData);
	signal_data->folder = g_object_ref (folder);
	signal_data->folder_name = old_name;  /* transfer ownership */

	camel_session_idle_add (
		session, G_PRIORITY_DEFAULT_IDLE,
		folder_emit_renamed_cb,
		signal_data, (GDestroyNotify) signal_data_free);
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

	camel_folder_lock (folder, CAMEL_FOLDER_CHANGE_LOCK);

	pending_changes = folder->priv->pending_changes;

	if (pending_changes == NULL) {
		CamelStore *parent_store;
		CamelService *service;
		CamelSession *session;
		SignalData *signal_data;

		parent_store = camel_folder_get_parent_store (folder);

		service = CAMEL_SERVICE (parent_store);
		session = camel_service_get_session (service);

		pending_changes = camel_folder_change_info_new ();
		folder->priv->pending_changes = pending_changes;

		signal_data = g_slice_new0 (SignalData);
		signal_data->folder = g_object_ref (folder);

		camel_session_idle_add (
			session, G_PRIORITY_LOW,
			folder_emit_changed_cb,
			signal_data, (GDestroyNotify) signal_data_free);
	}

	camel_folder_change_info_cat (pending_changes, changes);

	camel_folder_unlock (folder, CAMEL_FOLDER_CHANGE_LOCK);
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
 * camel_folder_free_nop:
 * @folder: a #CamelFolder
 * @array: an array of uids or #CamelMessageInfo
 *
 * "Frees" the provided array by doing nothing. Used by #CamelFolder
 * subclasses as an implementation for free_uids, or free_summary when
 * the returned array is "static" information and should not be freed.
 **/
void
camel_folder_free_nop (CamelFolder *folder,
                       GPtrArray *array)
{
	;
}

/**
 * camel_folder_free_shallow:
 * @folder: a #CamelFolder
 * @array: an array of uids or #CamelMessageInfo
 *
 * Frees the provided array but not its contents. Used by #CamelFolder
 * subclasses as an implementation for free_uids or free_summary when
 * the returned array needs to be freed but its contents come from
 * "static" information.
 **/
void
camel_folder_free_shallow (CamelFolder *folder,
                           GPtrArray *array)
{
	g_ptr_array_free (array, TRUE);
}

/**
 * camel_folder_free_deep:
 * @folder: a #CamelFolder
 * @array: an array of uids
 *
 * Frees the provided array and its contents. Used by #CamelFolder
 * subclasses as an implementation for free_uids when the provided
 * information was created explicitly by the corresponding get_ call.
 **/
void
camel_folder_free_deep (CamelFolder *folder,
                        GPtrArray *array)
{
	gint i;

	g_return_if_fail (array != NULL);

	for (i = 0; i < array->len; i++)
		g_free (array->pdata[i]);
	g_ptr_array_free (array, TRUE);
}

/**
 * camel_folder_lock:
 * @folder: a #CamelFolder
 * @lock: lock type to lock
 *
 * Locks @folder's @lock. Unlock it with camel_folder_unlock().
 *
 * Since: 2.32
 **/
void
camel_folder_lock (CamelFolder *folder,
                   CamelFolderLock lock)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	switch (lock) {
		case CAMEL_FOLDER_CHANGE_LOCK:
			g_static_mutex_lock (&folder->priv->change_lock);
			break;
		case CAMEL_FOLDER_REC_LOCK:
			if (folder->priv->skip_folder_lock == FALSE)
				g_static_rec_mutex_lock (&folder->priv->lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_folder_unlock:
 * @folder: a #CamelFolder
 * @lock: lock type to unlock
 *
 * Unlocks @folder's @lock, previously locked with camel_folder_lock().
 *
 * Since: 2.32
 **/
void
camel_folder_unlock (CamelFolder *folder,
                     CamelFolderLock lock)
{
	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	switch (lock) {
		case CAMEL_FOLDER_CHANGE_LOCK:
			g_static_mutex_unlock (&folder->priv->change_lock);
			break;
		case CAMEL_FOLDER_REC_LOCK:
			if (folder->priv->skip_folder_lock == FALSE)
				g_static_rec_mutex_unlock (&folder->priv->lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_folder_append_message_sync:
 * @folder: a #CamelFolder
 * @message: a #CamelMimeMessage
 * @info: a #CamelMessageInfo with additional flags/etc to set on the
 *        new message, or %NULL
 * @appended_uid: if non-%NULL, the UID of the appended message will
 *                be returned here, if it is known
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
	g_return_val_if_fail (class->append_message_sync != NULL, FALSE);

	camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
		return FALSE;
	}

	success = class->append_message_sync (
		folder, message, info, appended_uid, cancellable, error);
	CAMEL_CHECK_GERROR (folder, append_message_sync, success, error);

	camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);

	return success;
}

/**
 * camel_folder_append_message:
 * @folder a #CamelFolder
 * @message: a #CamelMimeMessage
 * @info: a #CamelMessageInfo with additional flags/etc to set on the
 *        new message, or %NULL
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->append_message != NULL);

	class->append_message (
		folder, message, info, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_folder_append_message_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @appended_uid: if non-%NULL, the UID of the appended message will
 *                be returned here, if it is known
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
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->append_message_finish != NULL, FALSE);

	return class->append_message_finish (
		folder, result, appended_uid, error);
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
	const gchar *display_name;
	const gchar *message;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->expunge_sync != NULL, FALSE);

	camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
		return FALSE;
	}

	message = _("Expunging folder '%s'");
	display_name = camel_folder_get_display_name (folder);
	camel_operation_push_message (cancellable, message, display_name);

	if (!(folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED)) {
		success = class->expunge_sync (folder, cancellable, error);
		CAMEL_CHECK_GERROR (folder, expunge_sync, success, error);
	}

	camel_operation_pop_message (cancellable);

	camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);

	return success;
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->expunge != NULL);

	class->expunge (folder, io_priority, cancellable, callback, user_data);
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
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->expunge_finish != NULL, FALSE);

	return class->expunge_finish (folder, result, error);
}

/**
 * camel_folder_fetch_messages_sync :
 * @folder: a #CamelFolder
 * @type: Type to specify fetch old or new messages.
 * #limit: Limit to specify the number of messages to download.
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Downloads old or new specified number of messages from the server. It is
 * optimized for mobile client usage. Desktop clients should keep away from
 * this api and use @camel_folder_refresh_info.
 *
 * Returns: %TRUE if there are more messages to fetch,
 *          %FALSE if there are no more messages
 *
 * Since: 3.4
 **/
gboolean
camel_folder_fetch_messages_sync (CamelFolder *folder,
                                  CamelFetchType type,
                                  gint limit,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelFolderClass *class;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);

	/* Some backends that wont support mobile
	 * mode, won't have this method implemented. */
	if (class->fetch_messages_sync == NULL)
		return FALSE;

	camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
		return FALSE;
	}

	success = class->fetch_messages_sync (
		folder, type, limit, cancellable, error);
	CAMEL_CHECK_GERROR (folder, fetch_messages_sync, success, error);

	camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);

	return success;
}

/**
 * camel_folder_fetch_messages:
 * @folder: a #CamelFolder
 * @type: Type to specify fetch old or new messages.
 * #limit: Limit to specify the number of messages to download.
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously download new or old messages from the server. It is assumes
 * that the client has only a window of interested messages of what server has.
 * And old/new type helps to expand that window.
 *
 * type = CAMEL_FETCH_OLD_MESSAGES: Downloads messages older than what the
 * client already has.
 * type = CAMEL_FETCH_NEW_MESSAGES: Downloads messages newer than what the
 * client already has.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_folder_fetch_messages_finish() to get the result of the operation.
 *
 * Since: 3.4
 **/
void
camel_folder_fetch_messages (CamelFolder *folder,
                             CamelFetchType type,
                             gint limit,
                             gint io_priority,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->fetch_messages != NULL);

	class->fetch_messages (
		folder, type, limit, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_folder_fetch_messages_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_folder_fetch_messages().
 *
 * Returns: %TRUE if there are more messages to fetch,
 *          %FALSE if there are no more messages
 *
 * Since: 3.4
 **/
gboolean
camel_folder_fetch_messages_finish (CamelFolder *folder,
                                    GAsyncResult *result,
                                    GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->fetch_messages_finish != NULL, FALSE);

	return class->fetch_messages_finish (folder, result, error);
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
 * Returns: a #CamelMimeMessage corresponding to the requested UID
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
	CamelMimeMessage *message = NULL;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (message_uid != NULL, NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_sync != NULL, NULL);

	camel_operation_push_message (
		cancellable, _("Retrieving message '%s' in %s"),
		message_uid, camel_folder_get_display_name (folder));

	if (class->get_message_cached) {
		/* Return cached message, if available locally; this should
		 * not do any network I/O, only check if message is already
		 * downloaded and return it quicker, not being blocked by
		 * the folder's lock.  Returning NULL is not considered as
		 * an error, it just means that the message is still
		 * to-be-downloaded. */
		message = class->get_message_cached (
			folder, message_uid, cancellable);
	}

	if (!message) {
		camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);

		/* Check for cancellation after locking. */
		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
			camel_operation_pop_message (cancellable);
			return NULL;
		}

		message = class->get_message_sync (
			folder, message_uid, cancellable, error);
		CAMEL_CHECK_GERROR (
			folder, get_message_sync, message != NULL, error);

		camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (message_uid != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->get_message != NULL);

	class->get_message (
		folder, message_uid, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_folder_get_message_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError or %NULL
 *
 * Finishes the operation started with camel_folder_get_message().
 *
 * Returns: a #CamelMimeMessage corresponding to the requested UID
 *
 * Since: 3.0
 **/
CamelMimeMessage *
camel_folder_get_message_finish (CamelFolder *folder,
                                 GAsyncResult *result,
                                 GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_message_finish != NULL, NULL);

	return class->get_message_finish (folder, result, error);
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
 * Returns: a #CamelFolderQuotaInfo, or %NULL
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
	const gchar *display_name;
	const gchar *message;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_quota_info_sync != NULL, NULL);

	message = _("Retrieving quota information for '%s'");
	display_name = camel_folder_get_display_name (folder);
	camel_operation_push_message (cancellable, message, display_name);

	quota_info = class->get_quota_info_sync (folder, cancellable, error);
	CAMEL_CHECK_GERROR (
		folder, get_quota_info_sync, quota_info != NULL, error);

	camel_operation_pop_message (cancellable);

	return quota_info;
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->get_quota_info != NULL);

	class->get_quota_info (
		folder, io_priority,
		cancellable, callback, user_data);
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
 * Returns: a #CamelFolderQuotaInfo, or %NULL
 *
 * Since: 3.2
 **/
CamelFolderQuotaInfo *
camel_folder_get_quota_info_finish (CamelFolder *folder,
                                    GAsyncResult *result,
                                    GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->get_quota_info_finish != NULL, NULL);

	return class->get_quota_info_finish (folder, result, error);
}

/**
 * camel_folder_purge_message_cache_sync :
 * @folder: a #CamelFolder
 * @start_uid: the start message UID
 * @end_uid: the end message UID
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Delete the local cache of all messages between these uids.
 *
 * Returns: %TRUE if success, %FALSE if there are any errors. 
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

	/* Some backends that wont support mobile
	 * mode, won't have this api implemented. */
	if (class->purge_message_cache_sync == NULL)
		return FALSE;

	camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
		return FALSE;
	}

	success = class->purge_message_cache_sync (
		folder, start_uid, end_uid, cancellable, error);
	CAMEL_CHECK_GERROR (folder, purge_message_cache_sync, success, error);

	camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);

	return success;
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->purge_message_cache != NULL);

	class->purge_message_cache (
		folder, start_uid, end_uid, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_folder_purge_message_cache_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_folder_purge_message_cache().
 *
 * Returns: %TRUE if cache is deleted, %FALSE if there are any errors
 *
 * Since: 3.4
 **/
gboolean
camel_folder_purge_message_cache_finish (CamelFolder *folder,
                                         GAsyncResult *result,
                                    GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->purge_message_cache_finish != NULL, FALSE);

	return class->purge_message_cache_finish (folder, result, error);
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
	const gchar *display_name;
	const gchar *message;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->refresh_info_sync != NULL, FALSE);

	camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
		return FALSE;
	}

	message = _("Refreshing folder '%s'");
	display_name = camel_folder_get_display_name (folder);
	camel_operation_push_message (cancellable, message, display_name);

	success = class->refresh_info_sync (folder, cancellable, error);
	CAMEL_CHECK_GERROR (folder, refresh_info_sync, success, error);

	camel_operation_pop_message (cancellable);

	camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);

	return success;
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->refresh_info != NULL);

	class->refresh_info (
		folder, io_priority, cancellable, callback, user_data);
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
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->refresh_info_finish != NULL, FALSE);

	return class->refresh_info_finish (folder, result, error);
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
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->synchronize_sync != NULL, FALSE);

	camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
		return FALSE;
	}

	if (!(folder->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED)) {
		success = class->synchronize_sync (
			folder, expunge, cancellable, error);
		CAMEL_CHECK_GERROR (folder, synchronize_sync, success, error);
	}

	camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);

	return success;
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->synchronize != NULL);

	class->synchronize (
		folder, expunge, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_folder_synchronize_finish:
 * @folder: a #CamelFolder
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_folder_synchronize().
 *
 * Returns: %TRUE on sucess, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_folder_synchronize_finish (CamelFolder *folder,
                                 GAsyncResult *result,
                                 GError **error)
{
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->synchronize_finish != NULL, FALSE);

	return class->synchronize_finish (folder, result, error);
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
	g_return_val_if_fail (class->get_message_sync != NULL, FALSE);

	camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
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

	camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);

	return success;
}

/**
 * camel_folder_synchronize_message;
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (folder));
	g_return_if_fail (message_uid != NULL);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_if_fail (class->synchronize_message != NULL);

	class->synchronize_message (
		folder, message_uid, io_priority,
		cancellable, callback, user_data);
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
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (class->synchronize_message_finish != NULL, FALSE);

	return class->synchronize_message_finish (folder, result, error);
}

/**
 * camel_folder_transfer_messages_to_sync:
 * @source: the source #CamelFolder
 * @message_uids: message UIDs in @source
 * @destination: the destination #CamelFolder
 * @delete_originals: whether or not to delete the original messages
 * @transferred_uids: if non-%NULL, the UIDs of the resulting messages
 *                    in @destination will be stored here, if known.
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
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_FOLDER (source), FALSE);
	g_return_val_if_fail (CAMEL_IS_FOLDER (destination), FALSE);
	g_return_val_if_fail (message_uids != NULL, FALSE);

	if (source == destination || message_uids->len == 0)
		return TRUE;

	if (source->priv->parent_store == destination->priv->parent_store) {
		/* If either folder is a vtrash, we need to use the
		 * vtrash transfer method. */
		if (CAMEL_IS_VTRASH_FOLDER (destination))
			class = CAMEL_FOLDER_GET_CLASS (destination);
		else
			class = CAMEL_FOLDER_GET_CLASS (source);
		success = class->transfer_messages_to_sync (
			source, message_uids, destination, delete_originals,
			transferred_uids, cancellable, error);
	} else
		success = folder_transfer_messages_to_sync (
			source, message_uids, destination, delete_originals,
			transferred_uids, cancellable, error);

	return success;
}

/**
 * camel_folder_transfer_messages_to:
 * @source: the source #CamelFolder
 * @message_uids: message UIDs in @source
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
	CamelFolderClass *class;

	g_return_if_fail (CAMEL_IS_FOLDER (source));
	g_return_if_fail (CAMEL_IS_FOLDER (destination));
	g_return_if_fail (message_uids != NULL);

	class = CAMEL_FOLDER_GET_CLASS (source);
	g_return_if_fail (class->transfer_messages_to != NULL);

	class->transfer_messages_to (
		source, message_uids, destination, delete_originals,
		io_priority, cancellable, callback, user_data);
}

/**
 * camel_folder_transfer_messages_to_finish:
 * @source: a #CamelFolder
 * @result: a #GAsyncResult
 * @transferred_uids: if non-%NULL, the UIDs of the resulting messages
 *                    in @destination will be stored here, if known.
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
	CamelFolderClass *class;

	g_return_val_if_fail (CAMEL_IS_FOLDER (source), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_FOLDER_GET_CLASS (source);
	g_return_val_if_fail (class->transfer_messages_to_finish != NULL, FALSE);

	return class->transfer_messages_to_finish (
		source, result, transferred_uids, error);
}

/**
 * camel_folder_change_info_new:
 *
 * Create a new folder change info structure.
 *
 * Change info structures are not MT-SAFE and must be
 * locked for exclusive access externally.
 *
 * Returns: a new #CamelFolderChangeInfo
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
 * @list: a list of uids
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
 * @list: a list of uids
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

	/* we dont need to copy this, as they've already been copied into our pool */
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

	/* always add to recent, but dont let anyone else know */
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

	/* always add to filter, but dont let anyone else know */
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
 * Concatenate one change info onto antoher.  Can be used to copy them
 * too.
 **/
void
camel_folder_change_info_cat (CamelFolderChangeInfo *info,
                              CamelFolderChangeInfo *source)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (source != NULL);

	change_info_cat (info, source->uid_added, camel_folder_change_info_add_uid);
	change_info_cat (info, source->uid_removed, camel_folder_change_info_remove_uid);
	change_info_cat (info, source->uid_changed, camel_folder_change_info_change_uid);
	change_info_cat (info, source->uid_recent, change_info_recent_uid);
	change_info_cat (info, source->priv->uid_filter, change_info_filter_uid);
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
		/* if it was changed then added, leave as changed */
		if (olduids == info->uid_removed) {
			g_ptr_array_remove_fast (olduids, olduid);
			g_ptr_array_add (info->uid_changed, olduid);
			g_hash_table_insert (p->uid_stored, olduid, info->uid_changed);
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
	if (p->uid_source) {
		g_hash_table_destroy (p->uid_source);
		p->uid_source = NULL;
	}
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

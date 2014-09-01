/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.c : class for a imap folder */
/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or modify it
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
#include <glib/gi18n-lib.h>

#include "camel-imapx-folder.h"
#include "camel-imapx-search.h"
#include "camel-imapx-server.h"
#include "camel-imapx-settings.h"
#include "camel-imapx-store.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-utils.h"

#include <stdlib.h>
#include <string.h>

#define d(...) camel_imapx_debug(debug, '?', __VA_ARGS__)

#define CAMEL_IMAPX_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_FOLDER, CamelIMAPXFolderPrivate))

struct _CamelIMAPXFolderPrivate {
	GMutex property_lock;
	GWeakRef mailbox;

	GMutex move_to_hash_table_lock;
	GHashTable *move_to_real_junk_uids;
	GHashTable *move_to_real_trash_uids;
};

/* The custom property ID is a CamelArg artifact.
 * It still identifies the property in state files. */
enum {
	PROP_0,
	PROP_MAILBOX,
	PROP_APPLY_FILTERS = 0x2501
};

G_DEFINE_TYPE (CamelIMAPXFolder, camel_imapx_folder, CAMEL_TYPE_OFFLINE_FOLDER)

static gboolean imapx_folder_get_apply_filters (CamelIMAPXFolder *folder);

static void
imapx_folder_claim_move_to_real_junk_uids (CamelIMAPXFolder *folder,
                                           GPtrArray *out_uids_to_copy)
{
	GList *keys;

	g_mutex_lock (&folder->priv->move_to_hash_table_lock);

	keys = g_hash_table_get_keys (folder->priv->move_to_real_junk_uids);
	g_hash_table_steal_all (folder->priv->move_to_real_junk_uids);

	g_mutex_unlock (&folder->priv->move_to_hash_table_lock);

	while (keys != NULL) {
		g_ptr_array_add (out_uids_to_copy, keys->data);
		keys = g_list_delete_link (keys, keys);
	}
}

static void
imapx_folder_claim_move_to_real_trash_uids (CamelIMAPXFolder *folder,
                                            GPtrArray *out_uids_to_copy)
{
	GList *keys;

	g_mutex_lock (&folder->priv->move_to_hash_table_lock);

	keys = g_hash_table_get_keys (folder->priv->move_to_real_trash_uids);
	g_hash_table_steal_all (folder->priv->move_to_real_trash_uids);

	g_mutex_unlock (&folder->priv->move_to_hash_table_lock);

	while (keys != NULL) {
		g_ptr_array_add (out_uids_to_copy, keys->data);
		keys = g_list_delete_link (keys, keys);
	}
}

static gboolean
imapx_folder_get_apply_filters (CamelIMAPXFolder *folder)
{
	g_return_val_if_fail (folder != NULL, FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_FOLDER (folder), FALSE);

	return folder->apply_filters;
}

static void
imapx_folder_set_apply_filters (CamelIMAPXFolder *folder,
                                gboolean apply_filters)
{
	g_return_if_fail (folder != NULL);
	g_return_if_fail (CAMEL_IS_IMAPX_FOLDER (folder));

	if (folder->apply_filters == apply_filters)
		return;

	folder->apply_filters = apply_filters;

	g_object_notify (G_OBJECT (folder), "apply-filters");
}

static void
imapx_folder_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_APPLY_FILTERS:
			imapx_folder_set_apply_filters (
				CAMEL_IMAPX_FOLDER (object),
				g_value_get_boolean (value));
			return;

		case PROP_MAILBOX:
			camel_imapx_folder_set_mailbox (
				CAMEL_IMAPX_FOLDER (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_folder_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_APPLY_FILTERS:
			g_value_set_boolean (
				value,
				imapx_folder_get_apply_filters (
				CAMEL_IMAPX_FOLDER (object)));
			return;

		case PROP_MAILBOX:
			g_value_take_object (
				value,
				camel_imapx_folder_ref_mailbox (
				CAMEL_IMAPX_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_folder_dispose (GObject *object)
{
	CamelIMAPXFolder *folder = CAMEL_IMAPX_FOLDER (object);
	CamelStore *store;

	if (folder->cache != NULL) {
		g_object_unref (folder->cache);
		folder->cache = NULL;
	}

	if (folder->search != NULL) {
		g_object_unref (folder->search);
		folder->search = NULL;
	}

	store = camel_folder_get_parent_store (CAMEL_FOLDER (folder));
	if (store != NULL) {
		camel_store_summary_disconnect_folder_summary (
			CAMEL_IMAPX_STORE (store)->summary,
			CAMEL_FOLDER (folder)->summary);
	}

	g_weak_ref_set (&folder->priv->mailbox, NULL);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_folder_parent_class)->dispose (object);
}

static void
imapx_folder_finalize (GObject *object)
{
	CamelIMAPXFolder *folder = CAMEL_IMAPX_FOLDER (object);

	g_mutex_clear (&folder->search_lock);
	g_mutex_clear (&folder->stream_lock);

	g_mutex_clear (&folder->priv->property_lock);

	g_mutex_clear (&folder->priv->move_to_hash_table_lock);
	g_hash_table_destroy (folder->priv->move_to_real_junk_uids);
	g_hash_table_destroy (folder->priv->move_to_real_trash_uids);

	g_weak_ref_clear (&folder->priv->mailbox);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_folder_parent_class)->finalize (object);
}

/* Algorithm for selecting a folder:
 *
 *  - If uidvalidity == old uidvalidity
 *    and exsists == old exists
 *    and recent == old recent
 *    and unseen == old unseen
 *    Assume our summary is correct
 *  for each summary item
 *    mark the summary item as 'old/not updated'
 *  rof
 *  fetch flags from 1:*
 *  for each fetch response
 *    info = summary[index]
 *    if info.uid != uid
 *      info = summary_by_uid[uid]
 *    fi
 *    if info == NULL
 *      create new info @ index
 *    fi
 *    if got.flags
 *      update flags
 *    fi
 *    if got.header
 *      update based on header
 *      mark as retrieved
 *    else if got.body
 *      update based on imap body
 *      mark as retrieved
 *    fi
 *
 *  Async fetch response:
 *    info = summary[index]
 *    if info == null
 *       if uid == null
 *          force resync/select?
 *       info = empty @ index
 *    else if uid && info.uid != uid
 *       force a resync?
 *       return
 *    fi
 *
 *    if got.flags {
 *      info.flags = flags
 *    }
 *    if got.header {
 *      info.init (header)
 *      info.empty = false
 *    }
 *
 * info.state - 2 bit field in flags
 *   0 = empty, nothing set
 *   1 = uid & flags set
 *   2 = update required
 *   3 = up to date
 */

static void
imapx_search_free (CamelFolder *folder,
                   GPtrArray *uids)
{
	CamelIMAPXFolder *imapx_folder;

	imapx_folder = CAMEL_IMAPX_FOLDER (folder);

	g_return_if_fail (imapx_folder->search);

	g_mutex_lock (&imapx_folder->search_lock);

	camel_folder_search_free_result (imapx_folder->search, uids);

	g_mutex_unlock (&imapx_folder->search_lock);
}

static GPtrArray *
imapx_search_by_uids (CamelFolder *folder,
                      const gchar *expression,
                      GPtrArray *uids,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelIMAPXFolder *imapx_folder;
	CamelIMAPXSearch *imapx_search;
	GPtrArray *matches;

	if (uids->len == 0)
		return g_ptr_array_new ();

	imapx_folder = CAMEL_IMAPX_FOLDER (folder);

	g_mutex_lock (&imapx_folder->search_lock);

	imapx_search = CAMEL_IMAPX_SEARCH (imapx_folder->search);

	camel_folder_search_set_folder (imapx_folder->search, folder);
	camel_imapx_search_set_cancellable_and_error (imapx_search, cancellable, error);

	matches = camel_folder_search_search (
		imapx_folder->search, expression, uids, cancellable, error);

	camel_imapx_search_set_cancellable_and_error (imapx_search, NULL, NULL);

	g_mutex_unlock (&imapx_folder->search_lock);

	return matches;
}

static guint32
imapx_count_by_expression (CamelFolder *folder,
                           const gchar *expression,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelIMAPXFolder *imapx_folder;
	CamelIMAPXSearch *imapx_search;
	guint32 matches;

	imapx_folder = CAMEL_IMAPX_FOLDER (folder);

	g_mutex_lock (&imapx_folder->search_lock);

	imapx_search = CAMEL_IMAPX_SEARCH (imapx_folder->search);

	camel_folder_search_set_folder (imapx_folder->search, folder);
	camel_imapx_search_set_cancellable_and_error (imapx_search, cancellable, error);

	matches = camel_folder_search_count (
		imapx_folder->search, expression, cancellable, error);

	camel_imapx_search_set_cancellable_and_error (imapx_search, NULL, NULL);

	g_mutex_unlock (&imapx_folder->search_lock);

	return matches;
}

static GPtrArray *
imapx_search_by_expression (CamelFolder *folder,
                            const gchar *expression,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelIMAPXFolder *imapx_folder;
	CamelIMAPXSearch *imapx_search;
	GPtrArray *matches;

	imapx_folder = CAMEL_IMAPX_FOLDER (folder);

	g_mutex_lock (&imapx_folder->search_lock);

	imapx_search = CAMEL_IMAPX_SEARCH (imapx_folder->search);

	camel_folder_search_set_folder (imapx_folder->search, folder);
	camel_imapx_search_set_cancellable_and_error (imapx_search, cancellable, error);

	matches = camel_folder_search_search (
		imapx_folder->search, expression, NULL, cancellable, error);

	camel_imapx_search_set_cancellable_and_error (imapx_search, NULL, NULL);

	g_mutex_unlock (&imapx_folder->search_lock);

	return matches;
}

static gchar *
imapx_get_filename (CamelFolder *folder,
                    const gchar *uid,
                    GError **error)
{
	CamelIMAPXFolder *imapx_folder;

	imapx_folder = CAMEL_IMAPX_FOLDER (folder);

	return camel_data_cache_get_filename (
		imapx_folder->cache, "cache", uid);
}

static gboolean
imapx_append_message_sync (CamelFolder *folder,
                           CamelMimeMessage *message,
                           CamelMessageInfo *info,
                           gchar **appended_uid,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelStore *store;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	const gchar *folder_name;
	GError *local_error = NULL;
	gboolean success = FALSE;

	if (appended_uid != NULL)
		*appended_uid = NULL;

	store = camel_folder_get_parent_store (folder);
	folder_name = camel_folder_get_full_name (folder);

	imapx_store = CAMEL_IMAPX_STORE (store);
	imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	mailbox = camel_imapx_folder_list_mailbox (
		CAMEL_IMAPX_FOLDER (folder), cancellable, error);

	if (mailbox == NULL) {
		camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		goto exit;
	}

	success = camel_imapx_server_append_message (
		imapx_server, mailbox, folder->summary,
		CAMEL_IMAPX_FOLDER (folder)->cache, message,
		info, appended_uid, cancellable, &local_error);

	camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, &local_error);
		if (imapx_server) {
			success = camel_imapx_server_append_message (
				imapx_server, mailbox, folder->summary,
				CAMEL_IMAPX_FOLDER (folder)->cache, message,
				info, appended_uid, cancellable, &local_error);

			camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		}
	}

	if (local_error)
		g_propagate_error (error, local_error);

exit:
	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);

	return success;
}

static gboolean
imapx_expunge_sync (CamelFolder *folder,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelStore *store;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	GError *local_error = NULL;
	const gchar *folder_name;
	gboolean success = FALSE;

	store = camel_folder_get_parent_store (folder);
	folder_name = camel_folder_get_full_name (folder);

	imapx_store = CAMEL_IMAPX_STORE (store);
	imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	mailbox = camel_imapx_folder_list_mailbox (
		CAMEL_IMAPX_FOLDER (folder), cancellable, error);

	if (mailbox == NULL) {
		camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		goto exit;
	}

	if ((store->flags & CAMEL_STORE_VTRASH) == 0) {
		CamelFolder *trash;
		const gchar *full_name;

		full_name = camel_folder_get_full_name (folder);

		trash = camel_store_get_trash_folder_sync (store, cancellable, &local_error);

		if (local_error == NULL && trash && (folder == trash || g_ascii_strcasecmp (full_name, camel_folder_get_full_name (trash)) == 0)) {
			CamelMessageInfo *info;
			GPtrArray *known_uids;
			gint ii;

			camel_folder_summary_lock (folder->summary);

			camel_folder_summary_prepare_fetch_all (folder->summary, NULL);
			known_uids = camel_folder_summary_get_array (folder->summary);

			/* it's a real trash folder, thus delete all mails from there */
			for (ii = 0; known_uids && ii < known_uids->len; ii++) {
				info = camel_folder_summary_get (folder->summary, g_ptr_array_index (known_uids, ii));
				if (info) {
					camel_message_info_set_flags (info, CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
					camel_message_info_unref (info);
				}
			}

			camel_folder_summary_unlock (folder->summary);

			camel_folder_summary_free_array (known_uids);
		}

		g_clear_object (&trash);
		g_clear_error (&local_error);
	}

	success = camel_imapx_server_expunge (imapx_server, mailbox, cancellable, &local_error);

	camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, &local_error);
		if (imapx_server) {
			success = camel_imapx_server_expunge (imapx_server, mailbox, cancellable, &local_error);

			camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		}
	}

	if (local_error)
		g_propagate_error (error, local_error);

exit:
	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);

	return success;
}

static CamelMimeMessage *
imapx_get_message_cached (CamelFolder *folder,
			  const gchar *message_uid,
			  GCancellable *cancellable)
{
	CamelIMAPXFolder *imapx_folder;
	CamelMimeMessage *msg = NULL;
	CamelStream *stream = NULL;
	GIOStream *base_stream;

	g_return_val_if_fail (CAMEL_IS_IMAPX_FOLDER (folder), NULL);
	g_return_val_if_fail (message_uid != NULL, NULL);

	imapx_folder = CAMEL_IMAPX_FOLDER (folder);

	base_stream = camel_data_cache_get (imapx_folder->cache, "cur", message_uid, NULL);
	if (base_stream != NULL) {
		stream = camel_stream_new (base_stream);
		g_object_unref (base_stream);
	}

	if (stream != NULL) {
		gboolean success;

		msg = camel_mime_message_new ();

		g_mutex_lock (&imapx_folder->stream_lock);
		success = camel_data_wrapper_construct_from_stream_sync (
			CAMEL_DATA_WRAPPER (msg), stream, cancellable, NULL);
		if (!success) {
			g_object_unref (msg);
			msg = NULL;
		}
		g_mutex_unlock (&imapx_folder->stream_lock);
		g_object_unref (stream);
	}

	return msg;
}

static CamelMimeMessage *
imapx_get_message_sync (CamelFolder *folder,
                        const gchar *uid,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelMimeMessage *msg = NULL;
	CamelStream *stream;
	CamelStore *store;
	CamelIMAPXFolder *imapx_folder;
	GIOStream *base_stream;
	const gchar *path = NULL;
	gboolean offline_message = FALSE;
	GError *local_error = NULL;

	imapx_folder = CAMEL_IMAPX_FOLDER (folder);
	store = camel_folder_get_parent_store (folder);

	if (!strchr (uid, '-'))
		path = "cur";
	else {
		path = "new";
		offline_message = TRUE;
	}

	base_stream = camel_data_cache_get (
		imapx_folder->cache, path, uid, NULL);
	if (base_stream != NULL) {
		stream = camel_stream_new (base_stream);
		g_object_unref (base_stream);
	} else {
		CamelIMAPXServer *imapx_server;
		CamelIMAPXMailbox *mailbox;
		const gchar *folder_name;

		if (offline_message) {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID_UID,
				"Offline message vanished from disk: %s", uid);
			return NULL;
		}

		folder_name = camel_folder_get_full_name (folder);
		imapx_server = camel_imapx_store_ref_server (
			CAMEL_IMAPX_STORE (store), folder_name, FALSE, cancellable, error);

		if (imapx_server == NULL)
			return NULL;

		mailbox = camel_imapx_folder_list_mailbox (
			CAMEL_IMAPX_FOLDER (folder), cancellable, error);

		if (mailbox == NULL) {
			camel_imapx_store_folder_op_done (CAMEL_IMAPX_STORE (store), imapx_server, folder_name);
			g_object_unref (imapx_server);
			return NULL;
		}

		stream = camel_imapx_server_get_message (
			imapx_server, mailbox, folder->summary,
			CAMEL_IMAPX_FOLDER (folder)->cache, uid,
			cancellable, &local_error);

		camel_imapx_store_folder_op_done (CAMEL_IMAPX_STORE (store), imapx_server, folder_name);

		while (!stream && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
			g_clear_error (&local_error);
			g_clear_object (&imapx_server);

			imapx_server = camel_imapx_store_ref_server (CAMEL_IMAPX_STORE (store), folder_name, FALSE, cancellable, &local_error);
			if (imapx_server) {
				stream = camel_imapx_server_get_message (
					imapx_server, mailbox, folder->summary,
					CAMEL_IMAPX_FOLDER (folder)->cache, uid,
					cancellable, &local_error);

				camel_imapx_store_folder_op_done (CAMEL_IMAPX_STORE (store), imapx_server, folder_name);
			}
		}

		if (local_error)
			g_propagate_error (error, local_error);

		g_clear_object (&mailbox);
		g_clear_object (&imapx_server);
	}

	if (stream != NULL) {
		gboolean success;

		msg = camel_mime_message_new ();

		g_mutex_lock (&imapx_folder->stream_lock);
		success = camel_data_wrapper_construct_from_stream_sync (
			CAMEL_DATA_WRAPPER (msg), stream, cancellable, error);
		if (!success) {
			g_object_unref (msg);
			msg = NULL;
		}
		g_mutex_unlock (&imapx_folder->stream_lock);
		g_object_unref (stream);
	}

	if (msg != NULL) {
		CamelMessageInfo *mi;

		mi = camel_folder_summary_get (folder->summary, uid);
		if (mi != NULL) {
			CamelMessageFlags flags;
			gboolean has_attachment;

			flags = camel_message_info_flags (mi);
			has_attachment = camel_mime_message_has_attachment (msg);
			if (((flags & CAMEL_MESSAGE_ATTACHMENTS) && !has_attachment) ||
			    ((flags & CAMEL_MESSAGE_ATTACHMENTS) == 0 && has_attachment)) {
				camel_message_info_set_flags (
					mi, CAMEL_MESSAGE_ATTACHMENTS,
					has_attachment ? CAMEL_MESSAGE_ATTACHMENTS : 0);
			}

			camel_message_info_unref (mi);
		}
	}

	return msg;
}

static CamelFolderQuotaInfo *
imapx_get_quota_info_sync (CamelFolder *folder,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelStore *store;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	CamelFolderQuotaInfo *quota_info = NULL;
	const gchar *folder_name;
	gchar **quota_roots;
	gboolean success = FALSE;
	GError *local_error = NULL;

	store = camel_folder_get_parent_store (folder);
	folder_name = camel_folder_get_full_name (folder);

	imapx_store = CAMEL_IMAPX_STORE (store);
	imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	mailbox = camel_imapx_folder_list_mailbox (
		CAMEL_IMAPX_FOLDER (folder), cancellable, error);
	if (mailbox == NULL) {
		camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		goto exit;
	}

	success = camel_imapx_server_update_quota_info (imapx_server, mailbox, cancellable, &local_error);

	camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, &local_error);
		if (imapx_server) {
			success = camel_imapx_server_update_quota_info (imapx_server, mailbox, cancellable, &local_error);

			camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		}
	}

	if (local_error)
		g_propagate_error (error, local_error);

	if (!success)
		goto exit;

	quota_roots = camel_imapx_mailbox_dup_quota_roots (mailbox);

	/* XXX Just return info for the first quota root, I guess. */
	if (quota_roots != NULL && quota_roots[0] != NULL) {
		quota_info = camel_imapx_store_dup_quota_info (
			CAMEL_IMAPX_STORE (store), quota_roots[0]);
	}

	g_strfreev (quota_roots);

	if (quota_info == NULL)
		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("No quota information available for folder '%s'"),
			camel_folder_get_full_name (folder));

exit:
	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);

	return quota_info;
}

static gboolean
imapx_purge_message_cache_sync (CamelFolder *folder,
                                gchar *start_uid,
                                gchar *end_uid,
                                GCancellable *cancellable,
                                GError **error)
{
	/* Not Implemented for now. */
	return TRUE;
}

static gboolean
imapx_refresh_info_sync (CamelFolder *folder,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelStore *store;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	CamelFolderChangeInfo *changes;
	gchar *folder_name;
	gboolean success = FALSE;
	GError *local_error = NULL;

	store = camel_folder_get_parent_store (folder);

	/* Not connected, thus skip the operation */
	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		return TRUE;

	folder_name = g_strdup (camel_folder_get_full_name (folder));
	imapx_store = CAMEL_IMAPX_STORE (store);
	imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, TRUE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	mailbox = camel_imapx_folder_list_mailbox (
		CAMEL_IMAPX_FOLDER (folder), cancellable, error);

	if (mailbox == NULL) {
		camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		goto exit;
	}

	changes = camel_imapx_server_refresh_info (imapx_server, mailbox, cancellable, &local_error);

	camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, TRUE, cancellable, &local_error);
		if (imapx_server) {
			changes = camel_imapx_server_refresh_info (imapx_server, mailbox, cancellable, &local_error);

			camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		}
	}

	if (local_error)
		g_propagate_error (error, local_error);

	if (changes != NULL) {
		if (camel_folder_change_info_changed (changes))
			camel_folder_changed (folder, changes);
		camel_folder_change_info_free (changes);
		success = TRUE;
	}

exit:
	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);
	g_free (folder_name);

	return success;
}

/* Helper for imapx_synchronize_sync() */
static gboolean
imapx_move_to_real_junk (CamelIMAPXServer *imapx_server,
                         CamelFolder *folder,
                         GCancellable *cancellable,
                         gboolean *out_need_to_expunge,
                         GError **error)
{
	CamelIMAPXFolder *imapx_folder;
	CamelIMAPXMailbox *mailbox;
	CamelIMAPXSettings *settings;
	GPtrArray *uids_to_copy;
	gchar *real_junk_path = NULL;
	gboolean success = TRUE;

	*out_need_to_expunge = FALSE;

	/* Caller already obtained the mailbox from the folder,
	 * so the folder should still have it readily available. */
	imapx_folder = CAMEL_IMAPX_FOLDER (folder);
	mailbox = camel_imapx_folder_ref_mailbox (imapx_folder);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	uids_to_copy = g_ptr_array_new_with_free_func (
		(GDestroyNotify) camel_pstring_free);

	settings = camel_imapx_server_ref_settings (imapx_server);
	if (camel_imapx_settings_get_use_real_junk_path (settings)) {
		real_junk_path =
			camel_imapx_settings_dup_real_junk_path (settings);
		imapx_folder_claim_move_to_real_junk_uids (
			imapx_folder, uids_to_copy);
	}
	g_object_unref (settings);

	if (uids_to_copy->len > 0) {
		CamelIMAPXStore *imapx_store;
		CamelIMAPXMailbox *destination = NULL;

		imapx_store = camel_imapx_server_ref_store (imapx_server);

		if (real_junk_path != NULL) {
			folder = camel_store_get_folder_sync (
				CAMEL_STORE (imapx_store),
				real_junk_path, 0,
				cancellable, error);
		} else {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID_PATH,
				_("No destination folder specified"));
			folder = NULL;
		}

		if (folder != NULL) {
			destination = camel_imapx_folder_list_mailbox (
				CAMEL_IMAPX_FOLDER (folder),
				cancellable, error);
			g_object_unref (folder);
		}

		/* Avoid duplicating messages in the Junk folder. */
		if (destination == mailbox) {
			success = TRUE;
		} else if (destination != NULL) {
			success = camel_imapx_server_copy_message (
				imapx_server,
				mailbox, destination,
				uids_to_copy, TRUE,
				cancellable, error);
			*out_need_to_expunge = success;
		} else {
			success = FALSE;
		}

		if (!success) {
			g_prefix_error (
				error, "%s: ",
				_("Unable to move junk messages"));
		}

		g_clear_object (&destination);
		g_clear_object (&imapx_store);
	}

	g_ptr_array_unref (uids_to_copy);
	g_free (real_junk_path);

	g_clear_object (&mailbox);

	return success;
}

/* Helper for imapx_synchronize_sync() */
static gboolean
imapx_move_to_real_trash (CamelIMAPXServer *imapx_server,
                          CamelFolder *folder,
                          GCancellable *cancellable,
                          gboolean *out_need_to_expunge,
                          GError **error)
{
	CamelIMAPXFolder *imapx_folder;
	CamelIMAPXMailbox *mailbox;
	CamelIMAPXSettings *settings;
	GPtrArray *uids_to_copy;
	gchar *real_trash_path = NULL;
	gboolean success = TRUE;

	*out_need_to_expunge = FALSE;

	/* Caller already obtained the mailbox from the folder,
	 * so the folder should still have it readily available. */
	imapx_folder = CAMEL_IMAPX_FOLDER (folder);
	mailbox = camel_imapx_folder_ref_mailbox (imapx_folder);
	g_return_val_if_fail (mailbox != NULL, FALSE);

	uids_to_copy = g_ptr_array_new_with_free_func (
		(GDestroyNotify) camel_pstring_free);

	settings = camel_imapx_server_ref_settings (imapx_server);
	if (camel_imapx_settings_get_use_real_trash_path (settings)) {
		real_trash_path =
			camel_imapx_settings_dup_real_trash_path (settings);
		imapx_folder_claim_move_to_real_trash_uids (
			CAMEL_IMAPX_FOLDER (folder), uids_to_copy);
	}
	g_object_unref (settings);

	if (uids_to_copy->len > 0) {
		CamelIMAPXStore *imapx_store;
		CamelIMAPXMailbox *destination = NULL;

		imapx_store = camel_imapx_server_ref_store (imapx_server);

		if (real_trash_path != NULL) {
			folder = camel_store_get_folder_sync (
				CAMEL_STORE (imapx_store),
				real_trash_path, 0,
				cancellable, error);
		} else {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID_PATH,
				_("No destination folder specified"));
			folder = NULL;
		}

		if (folder != NULL) {
			destination = camel_imapx_folder_list_mailbox (
				CAMEL_IMAPX_FOLDER (folder),
				cancellable, error);
			g_object_unref (folder);
		}

		/* Avoid duplicating messages in the Trash folder. */
		if (destination == mailbox) {
			success = TRUE;
		} else if (destination != NULL) {
			success = camel_imapx_server_copy_message (
				imapx_server,
				mailbox, destination,
				uids_to_copy, TRUE,
				cancellable, error);
			*out_need_to_expunge = success;
		} else {
			success = FALSE;
		}

		if (!success) {
			g_prefix_error (
				error, "%s: ",
				_("Unable to move deleted messages"));
		}

		g_clear_object (&destination);
		g_clear_object (&imapx_store);
	}

	g_ptr_array_unref (uids_to_copy);
	g_free (real_trash_path);

	g_clear_object (&mailbox);

	return success;
}

static gboolean
imapx_synchronize_sync (CamelFolder *folder,
                        gboolean expunge,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelStore *store;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	const gchar *folder_name;
	gboolean need_to_expunge;
	gboolean success = FALSE;
	GError *local_error = NULL;

	store = camel_folder_get_parent_store (folder);
	folder_name = camel_folder_get_full_name (folder);

	/* Not connected, thus skip the operation */
	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		return TRUE;

	imapx_store = CAMEL_IMAPX_STORE (store);
	/* while it can be expensive job, do not treat it as such, to avoid a blockage
	   by really expensive jobs */
	imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	mailbox = camel_imapx_folder_list_mailbox (
		CAMEL_IMAPX_FOLDER (folder), cancellable, error);
	if (mailbox == NULL) {
		camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		goto exit;
	}

	success = camel_imapx_server_sync_changes (imapx_server, mailbox, cancellable, &local_error);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);

		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, &local_error);
		if (imapx_server) {
			success = camel_imapx_server_sync_changes (imapx_server, mailbox, cancellable, &local_error);
		}
	}

	if (success) {
		success = imapx_move_to_real_junk (
			imapx_server, folder, cancellable,
			&need_to_expunge, error);
		expunge |= need_to_expunge;
	}

	if (success) {
		success = imapx_move_to_real_trash (
			imapx_server, folder, cancellable,
			&need_to_expunge, error);
		expunge |= need_to_expunge;
	}

	/* Sync twice - make sure deleted flags are written out,
	 * then sync again incase expunge changed anything */

	if (success && expunge) {
		success = camel_imapx_server_expunge (imapx_server, mailbox, cancellable, &local_error);

		while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
			camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);

			g_clear_error (&local_error);
			g_clear_object (&imapx_server);

			imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, &local_error);
			if (imapx_server) {
				success = camel_imapx_server_expunge (imapx_server, mailbox, cancellable, &local_error);
			}
		}
	}

	if (local_error)
		g_propagate_error (error, local_error);

	if (imapx_server)
		camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);

exit:
	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);

	return success;
}

static gboolean
imapx_synchronize_message_sync (CamelFolder *folder,
                                const gchar *uid,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelStore *store;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	const gchar *folder_name;
	gboolean success = FALSE;
	GError *local_error = NULL;

	store = camel_folder_get_parent_store (folder);
	folder_name = camel_folder_get_full_name (folder);

	imapx_store = CAMEL_IMAPX_STORE (store);
	imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	mailbox = camel_imapx_folder_list_mailbox (
		CAMEL_IMAPX_FOLDER (folder), cancellable, error);

	if (mailbox == NULL) {
		camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		goto exit;
	}

	success = camel_imapx_server_sync_message (
		imapx_server, mailbox, folder->summary,
		CAMEL_IMAPX_FOLDER (folder)->cache, uid,
		cancellable, &local_error);

	camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, &local_error);
		if (imapx_server) {
			success = camel_imapx_server_sync_message (
				imapx_server, mailbox, folder->summary,
				CAMEL_IMAPX_FOLDER (folder)->cache, uid,
				cancellable, &local_error);

			camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		}
	}

	if (local_error)
		g_propagate_error (error, local_error);

exit:
	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);

	return success;
}

static gboolean
imapx_transfer_messages_to_sync (CamelFolder *source,
                                 GPtrArray *uids,
                                 CamelFolder *dest,
                                 gboolean delete_originals,
                                 GPtrArray **transferred_uids,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelStore *store;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *src_mailbox = NULL;
	CamelIMAPXMailbox *dst_mailbox = NULL;
	const gchar *folder_name;
	gboolean success = FALSE;
	GError *local_error = NULL;

	store = camel_folder_get_parent_store (source);
	folder_name = camel_folder_get_full_name (source);

	imapx_store = CAMEL_IMAPX_STORE (store);
	imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	src_mailbox = camel_imapx_folder_list_mailbox (
		CAMEL_IMAPX_FOLDER (source), cancellable, error);

	if (src_mailbox == NULL) {
		camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		goto exit;
	}

	dst_mailbox = camel_imapx_folder_list_mailbox (
		CAMEL_IMAPX_FOLDER (dest), cancellable, error);

	if (dst_mailbox == NULL) {
		camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		goto exit;
	}

	success = camel_imapx_server_copy_message (
		imapx_server, src_mailbox, dst_mailbox, uids,
		delete_originals, cancellable, &local_error);

	camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, FALSE, cancellable, &local_error);
		if (imapx_server) {
			success = camel_imapx_server_copy_message (
				imapx_server, src_mailbox, dst_mailbox, uids,
				delete_originals, cancellable, &local_error);

			camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
		}
	}

	if (local_error)
		g_propagate_error (error, local_error);

	/* Update destination folder only if it's not frozen,
	 * to avoid updating for each "move" action on a single
	 * message while filtering. */
	if (!camel_folder_is_frozen (dest))
		imapx_refresh_info_sync (dest, cancellable, NULL);

exit:
	g_clear_object (&src_mailbox);
	g_clear_object (&dst_mailbox);
	g_clear_object (&imapx_server);

	return success;
}

static void
imapx_rename (CamelFolder *folder,
              const gchar *new_name)
{
	CamelStore *store;
	CamelIMAPXStore *imapx_store;
	const gchar *folder_name;

	store = camel_folder_get_parent_store (folder);
	imapx_store = CAMEL_IMAPX_STORE (store);

	camel_store_summary_disconnect_folder_summary (
		imapx_store->summary, folder->summary);

	/* Chain up to parent's rename() method. */
	CAMEL_FOLDER_CLASS (camel_imapx_folder_parent_class)->
		rename (folder, new_name);

	folder_name = camel_folder_get_full_name (folder);

	camel_store_summary_connect_folder_summary (
		imapx_store->summary, folder_name, folder->summary);
}

static void
camel_imapx_folder_class_init (CamelIMAPXFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	g_type_class_add_private (class, sizeof (CamelIMAPXFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_folder_set_property;
	object_class->get_property = imapx_folder_get_property;
	object_class->dispose = imapx_folder_dispose;
	object_class->finalize = imapx_folder_finalize;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->rename = imapx_rename;
	folder_class->search_by_expression = imapx_search_by_expression;
	folder_class->search_by_uids = imapx_search_by_uids;
	folder_class->count_by_expression = imapx_count_by_expression;
	folder_class->search_free = imapx_search_free;
	folder_class->get_filename = imapx_get_filename;
	folder_class->append_message_sync = imapx_append_message_sync;
	folder_class->expunge_sync = imapx_expunge_sync;
	folder_class->get_message_cached = imapx_get_message_cached;
	folder_class->get_message_sync = imapx_get_message_sync;
	folder_class->get_quota_info_sync = imapx_get_quota_info_sync;
	folder_class->purge_message_cache_sync = imapx_purge_message_cache_sync;
	folder_class->refresh_info_sync = imapx_refresh_info_sync;
	folder_class->synchronize_sync = imapx_synchronize_sync;
	folder_class->synchronize_message_sync = imapx_synchronize_message_sync;
	folder_class->transfer_messages_to_sync = imapx_transfer_messages_to_sync;

	g_object_class_install_property (
		object_class,
		PROP_APPLY_FILTERS,
		g_param_spec_boolean (
			"apply-filters",
			"Apply Filters",
			_("Apply message _filters to this folder"),
			FALSE,
			G_PARAM_READWRITE |
			CAMEL_PARAM_PERSISTENT));

	g_object_class_install_property (
		object_class,
		PROP_MAILBOX,
		g_param_spec_object (
			"mailbox",
			"Mailbox",
			"IMAP mailbox for this folder",
			CAMEL_TYPE_IMAPX_MAILBOX,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_imapx_folder_init (CamelIMAPXFolder *imapx_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (imapx_folder);
	GHashTable *move_to_real_junk_uids;
	GHashTable *move_to_real_trash_uids;

	move_to_real_junk_uids = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) camel_pstring_free,
		(GDestroyNotify) NULL);

	move_to_real_trash_uids = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) camel_pstring_free,
		(GDestroyNotify) NULL);

	imapx_folder->priv = CAMEL_IMAPX_FOLDER_GET_PRIVATE (imapx_folder);

	folder->folder_flags |= CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY;

	folder->permanent_flags =
		CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN |
		CAMEL_MESSAGE_USER;

	camel_folder_set_lock_async (folder, TRUE);

	g_mutex_init (&imapx_folder->priv->property_lock);

	g_mutex_init (&imapx_folder->priv->move_to_hash_table_lock);
	imapx_folder->priv->move_to_real_junk_uids = move_to_real_junk_uids;
	imapx_folder->priv->move_to_real_trash_uids = move_to_real_trash_uids;

	g_mutex_init (&imapx_folder->search_lock);
	g_mutex_init (&imapx_folder->stream_lock);

	g_weak_ref_init (&imapx_folder->priv->mailbox, NULL);
}

CamelFolder *
camel_imapx_folder_new (CamelStore *store,
                        const gchar *folder_dir,
                        const gchar *folder_name,
                        GError **error)
{
	CamelFolder *folder;
	CamelService *service;
	CamelSettings *settings;
	CamelIMAPXFolder *imapx_folder;
	const gchar *short_name;
	gchar *state_file;
	gboolean filter_all;
	gboolean filter_inbox;
	gboolean filter_junk;
	gboolean filter_junk_inbox;

	d ("opening imap folder '%s'\n", folder_dir);

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	g_object_get (
		settings,
		"filter-all", &filter_all,
		"filter-inbox", &filter_inbox,
		"filter-junk", &filter_junk,
		"filter-junk-inbox", &filter_junk_inbox,
		NULL);

	g_object_unref (settings);

	short_name = strrchr (folder_name, '/');
	if (short_name)
		short_name++;
	else
		short_name = folder_name;

	folder = g_object_new (
		CAMEL_TYPE_IMAPX_FOLDER,
		"display-name", short_name,
		"full_name", folder_name,
		"parent-store", store, NULL);

	folder->summary = camel_imapx_summary_new (folder);
	if (folder->summary == NULL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not create folder summary for %s"),
			short_name);
		return NULL;
	}

	imapx_folder = CAMEL_IMAPX_FOLDER (folder);
	imapx_folder->cache = camel_data_cache_new (folder_dir, error);
	if (imapx_folder->cache == NULL) {
		g_prefix_error (
			error, _("Could not create cache for %s: "),
			short_name);
		return NULL;
	}

	/* Ensure cache will never expire, otherwise
	 * it causes redownload of messages. */
	camel_data_cache_set_expire_age (imapx_folder->cache, -1);
	camel_data_cache_set_expire_access (imapx_folder->cache, -1);

	state_file = g_build_filename (folder_dir, "cmeta", NULL);
	camel_object_set_state_filename (CAMEL_OBJECT (folder), state_file);
	g_free (state_file);
	camel_object_state_read (CAMEL_OBJECT (folder));

	imapx_folder->search = camel_imapx_search_new (CAMEL_IMAPX_STORE (store));

	if (filter_all)
		folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

	if (camel_imapx_mailbox_is_inbox (folder_name)) {
		if (filter_inbox)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

		if (filter_junk)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
	} else {
		if (filter_junk && !filter_junk_inbox)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;

		if (imapx_folder_get_apply_filters (imapx_folder))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
	}

	camel_store_summary_connect_folder_summary (
		CAMEL_IMAPX_STORE (store)->summary,
		folder_name, folder->summary);

	return folder;
}

/**
 * camel_imapx_folder_ref_mailbox:
 * @folder: a #CamelIMAPXFolder
 *
 * Returns the #CamelIMAPXMailbox for @folder from the current IMAP server
 * connection, or %NULL if @folder's #CamelFolder:parent-store is disconnected
 * from the IMAP server.
 *
 * The returned #CamelIMAPXMailbox is referenced for thread-safety and
 * should be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #CamelIMAPXMailbox, or %NULL
 *
 * Since: 3.12
 **/
CamelIMAPXMailbox *
camel_imapx_folder_ref_mailbox (CamelIMAPXFolder *folder)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_FOLDER (folder), NULL);

	return g_weak_ref_get (&folder->priv->mailbox);
}

/**
 * camel_imapx_folder_set_mailbox:
 * @folder: a #CamelIMAPXFolder
 * @mailbox: a #CamelIMAPXMailbox
 *
 * Sets the #CamelIMAPXMailbox for @folder from the current IMAP server
 * connection.  Note that #CamelIMAPXFolder only holds a weak reference
 * on its #CamelIMAPXMailbox so that when the IMAP server connection is
 * lost, all mailbox instances are automatically destroyed.
 *
 * Since: 3.12
 **/
void
camel_imapx_folder_set_mailbox (CamelIMAPXFolder *folder,
                                CamelIMAPXMailbox *mailbox)
{
	CamelIMAPXSummary *imapx_summary;
	guint32 uidvalidity;

	g_return_if_fail (CAMEL_IS_IMAPX_FOLDER (folder));
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	g_weak_ref_set (&folder->priv->mailbox, mailbox);

	imapx_summary = CAMEL_IMAPX_SUMMARY (CAMEL_FOLDER (folder)->summary);
	uidvalidity = camel_imapx_mailbox_get_uidvalidity (mailbox);

	if (uidvalidity > 0 && uidvalidity != imapx_summary->validity)
		camel_imapx_folder_invalidate_local_cache (folder, uidvalidity);

	g_object_notify (G_OBJECT (folder), "mailbox");
}

/**
 * camel_imapx_folder_list_mailbox:
 * @folder: a #CamelIMAPXFolder
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Ensures that @folder's #CamelIMAPXFolder:mailbox property is set,
 * going so far as to issue a LIST command if necessary (but should
 * be a rarely needed last resort).
 *
 * If @folder's #CamelFolder:parent-store is disconnected from the IMAP
 * server or an error occurs during the LIST command, the function sets
 * @error and returns %NULL.
 *
 * The returned #CamelIMAPXMailbox is referenced for thread-safety and
 * should be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #CamelIMAPXMailbox, or %NULL
 *
 * Since: 3.12
 **/
CamelIMAPXMailbox *
camel_imapx_folder_list_mailbox (CamelIMAPXFolder *folder,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *server = NULL;
	CamelIMAPXMailbox *mailbox;
	CamelStore *parent_store;
	CamelStoreInfo *store_info;
	CamelIMAPXStoreInfo *imapx_store_info;
	gchar *folder_path = NULL;
	gchar *mailbox_name = NULL;
	gchar *pattern;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_FOLDER (folder), FALSE);

	/* First check if we already have a mailbox. */

	mailbox = camel_imapx_folder_ref_mailbox (folder);
	if (mailbox != NULL)
		goto exit;

	/* Obtain the mailbox name from the store summary. */

	folder_path = camel_folder_dup_full_name (CAMEL_FOLDER (folder));
	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (folder));

	imapx_store = CAMEL_IMAPX_STORE (parent_store);

	store_info = camel_store_summary_path (
		imapx_store->summary, folder_path);

	/* This should never fail.  We needed the CamelStoreInfo
	 * to instantiate the CamelIMAPXFolder in the first place. */
	g_return_val_if_fail (store_info != NULL, FALSE);

	imapx_store_info = (CamelIMAPXStoreInfo *) store_info;
	mailbox_name = g_strdup (imapx_store_info->mailbox_name);

	camel_store_summary_info_unref (imapx_store->summary, store_info);

	/* See if the CamelIMAPXStore already has the mailbox. */

	mailbox = camel_imapx_store_ref_mailbox (imapx_store, mailbox_name);
	if (mailbox != NULL) {
		camel_imapx_folder_set_mailbox (folder, mailbox);
		goto exit;
	}

	server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, error);
	if (server == NULL)
		goto exit;

	mailbox = camel_imapx_store_ref_mailbox (imapx_store, mailbox_name);
	if (mailbox != NULL) {
		camel_imapx_folder_set_mailbox (folder, mailbox);
		goto exit;
	}

	/* Last resort is to issue a LIST command.  Maintainer should
	 * monitor IMAP logs to make sure this is rarely if ever used. */

	pattern = camel_utf8_utf7 (mailbox_name);

	/* This creates a mailbox instance from the LIST response. */
	success = camel_imapx_server_list (server, pattern, 0, cancellable, &local_error);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&server);

		server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
		if (server) {
			success = camel_imapx_server_list (server, pattern, 0, cancellable, &local_error);
		}
	}

	if (local_error)
		g_propagate_error (error, local_error);

	g_free (pattern);

	if (!success)
		goto exit;

	/* This might still return NULL if the mailbox has a
	 * /NonExistent attribute.  Otherwise this should work. */
	mailbox = camel_imapx_store_ref_mailbox (imapx_store, mailbox_name);
	if (mailbox != NULL) {
		camel_imapx_folder_set_mailbox (folder, mailbox);
	} else {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_STATE,
			_("No IMAP mailbox available for folder '%s'"),
			camel_folder_get_display_name (CAMEL_FOLDER (folder)));
	}

exit:
	g_clear_object (&server);

	g_free (folder_path);
	g_free (mailbox_name);

	return mailbox;
}

/**
 * camel_imapx_folder_copy_message_map:
 * @folder: a #CamelIMAPXFolder
 *
 * Returns a #GSequence of 32-bit integers representing the locally cached
 * mapping of message sequence numbers to unique identifiers.
 *
 * Free the returns #GSequence with g_sequence_free().
 *
 * Returns: a #GSequence
 *
 * Since: 3.12
 **/
GSequence *
camel_imapx_folder_copy_message_map (CamelIMAPXFolder *folder)
{
	CamelFolderSummary *summary;
	GSequence *message_map;
	GPtrArray *array;
	guint ii;

	g_return_val_if_fail (CAMEL_IS_IMAPX_FOLDER (folder), NULL);

	summary = CAMEL_FOLDER (folder)->summary;
	array = camel_folder_summary_get_array (summary);
	camel_folder_sort_uids (CAMEL_FOLDER (folder), array);

	message_map = g_sequence_new (NULL);

	for (ii = 0; ii < array->len; ii++) {
		guint32 uid = strtoul (array->pdata[ii], NULL, 10);
		g_sequence_append (message_map, GUINT_TO_POINTER (uid));
	}

	camel_folder_summary_free_array (array);

	return message_map;
}

/**
 * camel_imapx_folder_add_move_to_real_junk:
 * @folder: a #CamelIMAPXFolder
 * @message_uid: a message UID
 *
 * Adds @message_uid to a pool of messages to be moved to a real junk
 * folder the next time @folder is explicitly synchronized by way of
 * camel_folder_synchronize() or camel_folder_synchronize_sync().
 *
 * This only applies when using a real folder to track junk messages,
 * as specified by the #CamelIMAPXSettings:use-real-junk-path setting.
 *
 * Since: 3.8
 **/
void
camel_imapx_folder_add_move_to_real_junk (CamelIMAPXFolder *folder,
                                          const gchar *message_uid)
{
	g_return_if_fail (CAMEL_IS_IMAPX_FOLDER (folder));
	g_return_if_fail (message_uid != NULL);

	g_mutex_lock (&folder->priv->move_to_hash_table_lock);

	g_hash_table_add (
		folder->priv->move_to_real_junk_uids,
		(gpointer) camel_pstring_strdup (message_uid));

	g_mutex_unlock (&folder->priv->move_to_hash_table_lock);
}

/**
 * camel_imapx_folder_add_move_to_real_trash:
 * @folder: a #CamelIMAPXFolder
 * @message_uid: a message UID
 *
 * Adds @message_uid to a pool of messages to be moved to a real trash
 * folder the next time @folder is explicitly synchronized by way of
 * camel_folder_synchronize() or camel_folder_synchronize_sync().
 *
 * This only applies when using a real folder to track deleted messages,
 * as specified by the #CamelIMAPXSettings:use-real-trash-path setting.
 *
 * Since: 3.8
 **/
void
camel_imapx_folder_add_move_to_real_trash (CamelIMAPXFolder *folder,
                                           const gchar *message_uid)
{
	g_return_if_fail (CAMEL_IS_IMAPX_FOLDER (folder));
	g_return_if_fail (message_uid != NULL);

	g_mutex_lock (&folder->priv->move_to_hash_table_lock);

	g_hash_table_add (
		folder->priv->move_to_real_trash_uids,
		(gpointer) camel_pstring_strdup (message_uid));

	g_mutex_unlock (&folder->priv->move_to_hash_table_lock);
}

/**
 * camel_imapx_folder_invalidate_local_cache:
 * @folder: a #CamelIMAPXFolder
 * @new_uidvalidity: the new UIDVALIDITY value
 *
 * Call this function when the IMAP server reports a different UIDVALIDITY
 * value than what is presently cached.  This means all cached message UIDs
 * are now invalid and must be discarded.
 *
 * The local cache for @folder is reset and the @new_uidvalidity value is
 * recorded in the newly-reset cache.
 *
 * Since: 3.10
 **/
void
camel_imapx_folder_invalidate_local_cache (CamelIMAPXFolder *folder,
                                           guint64 new_uidvalidity)
{
	CamelFolderSummary *summary;
	CamelFolderChangeInfo *changes;
	GPtrArray *array;
	guint ii;

	g_return_if_fail (CAMEL_IS_IMAPX_FOLDER (folder));
	g_return_if_fail (new_uidvalidity > 0);

	summary = CAMEL_FOLDER (folder)->summary;

	changes = camel_folder_change_info_new ();
	array = camel_folder_summary_get_array (summary);

	for (ii = 0; ii < array->len; ii++) {
		const gchar *uid = array->pdata[ii];
		camel_folder_change_info_change_uid (changes, uid);
	}

	CAMEL_IMAPX_SUMMARY (summary)->validity = new_uidvalidity;
	camel_folder_summary_touch (summary);
	camel_folder_summary_save_to_db (summary, NULL);

	camel_data_cache_clear (folder->cache, "cache");
	camel_data_cache_clear (folder->cache, "cur");

	camel_folder_changed (CAMEL_FOLDER (folder), changes);

	camel_folder_change_info_free (changes);
	camel_folder_summary_free_array (array);
}


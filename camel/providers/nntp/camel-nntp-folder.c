/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-nntp-folder.c : Class for a news folder
 *
 * Authors : Chris Toshok <toshok@ximian.com>
 *           Michael Zucchi <notzed@ximian.com>
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include "camel-nntp-folder.h"
#include "camel-nntp-private.h"
#include "camel-nntp-store.h"
#include "camel-nntp-store.h"
#include "camel-nntp-summary.h"

#define CAMEL_NNTP_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_NNTP_FOLDER, CamelNNTPFolderPrivate))

G_DEFINE_TYPE (CamelNNTPFolder, camel_nntp_folder, CAMEL_TYPE_DISCO_FOLDER)

static void
nntp_folder_dispose (GObject *object)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (object);

	camel_folder_summary_save_to_db (
		CAMEL_FOLDER (nntp_folder)->summary, NULL);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_nntp_folder_parent_class)->dispose (object);
}

static void
nntp_folder_finalize (GObject *object)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (object);

	g_mutex_free (nntp_folder->priv->search_lock);
	g_mutex_free (nntp_folder->priv->cache_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_nntp_folder_parent_class)->finalize (object);
}

gboolean
camel_nntp_folder_selected (CamelNNTPFolder *nntp_folder,
                            gchar *line,
                            GError **error)
{
	CamelFolder *folder;
	CamelStore *parent_store;

	folder = CAMEL_FOLDER (nntp_folder);
	parent_store = camel_folder_get_parent_store (folder);

	return camel_nntp_summary_check (
		CAMEL_NNTP_SUMMARY (folder->summary),
		CAMEL_NNTP_STORE (parent_store),
		line, nntp_folder->changes, error);
}

static gboolean
nntp_folder_refresh_info_online (CamelFolder *folder,
                                 GError **error)
{
	CamelStore *parent_store;
	CamelNNTPStore *nntp_store;
	CamelFolderChangeInfo *changes = NULL;
	CamelNNTPFolder *nntp_folder;
	gchar *line;
	gboolean success;

	parent_store = camel_folder_get_parent_store (folder);

	nntp_folder = CAMEL_NNTP_FOLDER (folder);
	nntp_store = CAMEL_NNTP_STORE (parent_store);

	camel_service_lock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	/* When invoked with no fmt, camel_nntp_command() just selects the folder
	   and should return zero. */
	success = !camel_nntp_command (
		nntp_store, error, nntp_folder, &line, NULL);

	if (camel_folder_change_info_changed(nntp_folder->changes)) {
		changes = nntp_folder->changes;
		nntp_folder->changes = camel_folder_change_info_new();
	}

	camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (changes) {
		camel_folder_changed (folder, changes);
		camel_folder_change_info_free (changes);
	}

	return success;
}

static void
unset_flagged_flag (const gchar *uid, CamelFolderSummary *summary)
{
	CamelMessageInfo *info;

	info = camel_folder_summary_uid (summary, uid);
	if (info) {
		CamelMessageInfoBase *base = (CamelMessageInfoBase *) info;

		if ((base->flags & CAMEL_MESSAGE_FOLDER_FLAGGED) != 0) {
			base->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
			base->dirty = TRUE;
		}

		camel_message_info_free (info);
	}
}

static gboolean
nntp_folder_sync (CamelFolder *folder, GError **error)
{
	CamelStore *parent_store;
	GPtrArray *changed;
	gboolean success;

	parent_store = camel_folder_get_parent_store (folder);

	camel_service_lock (CAMEL_SERVICE (parent_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	changed = camel_folder_summary_get_changed (folder->summary);
	if (changed) {
		g_ptr_array_foreach (changed, (GFunc) unset_flagged_flag, folder->summary);
		g_ptr_array_foreach (changed, (GFunc) camel_pstring_free, NULL);
		g_ptr_array_free (changed, TRUE);
		camel_folder_summary_touch (folder->summary);
	}
	success = !camel_folder_summary_save_to_db (folder->summary, error);
	camel_service_unlock (CAMEL_SERVICE (parent_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return success;
}

static gboolean
nntp_folder_sync_online (CamelFolder *folder, GError **error)
{
	return nntp_folder_sync (folder, error);
}

static gboolean
nntp_folder_sync_offline (CamelFolder *folder, GError **error)
{
	return nntp_folder_sync (folder, error);
}

static gchar *
nntp_get_filename (CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelStore *parent_store;
	CamelNNTPStore *nntp_store;
	gchar *article, *msgid;

	parent_store = camel_folder_get_parent_store (folder);
	nntp_store = CAMEL_NNTP_STORE (parent_store);

	article = alloca(strlen(uid)+1);
	strcpy(article, uid);
	msgid = strchr (article, ',');
	if (msgid == NULL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Internal error: UID in invalid format: %s"), uid);
		return NULL;
	}
	*msgid++ = 0;

	return camel_data_cache_get_filename (nntp_store->cache, "cache", msgid, error);
}

static CamelStream *
nntp_folder_download_message (CamelNNTPFolder *nntp_folder, const gchar *id, const gchar *msgid, GError **error)
{
	CamelFolder *folder;
	CamelStore *parent_store;
	CamelNNTPStore *nntp_store;
	CamelStream *stream = NULL;
	gint ret;
	gchar *line;

	folder = CAMEL_FOLDER (nntp_folder);
	parent_store = camel_folder_get_parent_store (folder);
	nntp_store = CAMEL_NNTP_STORE (parent_store);

	ret = camel_nntp_command (nntp_store, error, nntp_folder, &line, "article %s", id);
	if (ret == 220) {
		stream = camel_data_cache_add (nntp_store->cache, "cache", msgid, NULL);
		if (stream) {
			if (camel_stream_write_to_stream ((CamelStream *) nntp_store->stream, stream, error) == -1)
				goto fail;
			if (camel_stream_reset (stream, error) == -1)
				goto fail;
		} else {
			stream = g_object_ref (nntp_store->stream);
		}
	} else if (ret == 423 || ret == 430) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("Cannot get message %s: %s"), msgid, line);
	} else if (ret != -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cannot get message %s: %s"), msgid, line);
	}

	return stream;

fail:
	g_prefix_error (error, _("Cannot get message %s: "), msgid);

	return NULL;
}

static gboolean
nntp_folder_cache_message (CamelDiscoFolder *disco_folder,
                           const gchar *uid,
                           GError **error)
{
	CamelFolder *folder;
	CamelStore *parent_store;
	CamelNNTPStore *nntp_store;
	CamelStream *stream;
	gchar *article, *msgid;
	gboolean success = TRUE;

	folder = CAMEL_FOLDER (disco_folder);
	parent_store = camel_folder_get_parent_store (folder);
	nntp_store = CAMEL_NNTP_STORE (parent_store);

	article = alloca(strlen(uid)+1);
	strcpy(article, uid);
	msgid = strchr(article, ',');
	if (!msgid) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Internal error: UID in invalid format: %s"), uid);
		return FALSE;
	}
	*msgid++ = 0;

	camel_service_lock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	stream = nntp_folder_download_message (
		(CamelNNTPFolder *) disco_folder, article, msgid, error);
	if (stream)
		g_object_unref (stream);
	else
		success = FALSE;

	camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return success;
}

static CamelMimeMessage *
nntp_folder_get_message (CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelStore *parent_store;
	CamelMimeMessage *message = NULL;
	CamelNNTPStore *nntp_store;
	CamelFolderChangeInfo *changes;
	CamelNNTPFolder *nntp_folder;
	CamelStream *stream = NULL;
	gchar *article, *msgid;

	parent_store = camel_folder_get_parent_store (folder);

	nntp_folder = CAMEL_NNTP_FOLDER (folder);
	nntp_store = CAMEL_NNTP_STORE (parent_store);

	article = alloca(strlen(uid)+1);
	strcpy(article, uid);
	msgid = strchr (article, ',');
	if (msgid == NULL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Internal error: UID in invalid format: %s"), uid);
		return NULL;
	}
	*msgid++ = 0;

	camel_service_lock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	/* Lookup in cache, NEWS is global messageid's so use a global cache path */
	stream = camel_data_cache_get (nntp_store->cache, "cache", msgid, NULL);
	if (stream == NULL) {
		if (camel_disco_store_status ((CamelDiscoStore *) nntp_store) == CAMEL_DISCO_STORE_OFFLINE) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("This message is not currently available"));
			goto fail;
		}

		stream = nntp_folder_download_message (nntp_folder, article, msgid, error);
		if (stream == NULL)
			goto fail;
	}

	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) message, stream, error) == -1) {
		g_prefix_error (error, _("Cannot get message %s: "), uid);
		g_object_unref (message);
		message = NULL;
	}

	g_object_unref (stream);
fail:
	if (camel_folder_change_info_changed (nntp_folder->changes)) {
		changes = nntp_folder->changes;
		nntp_folder->changes = camel_folder_change_info_new ();
	} else {
		changes = NULL;
	}

	camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	if (changes) {
		camel_folder_changed (folder, changes);
		camel_folder_change_info_free (changes);
	}

	return message;
}

static GPtrArray*
nntp_folder_search_by_expression (CamelFolder *folder, const gchar *expression, GError **error)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (folder);
	GPtrArray *matches;

	CAMEL_NNTP_FOLDER_LOCK(nntp_folder, search_lock);

	if (nntp_folder->search == NULL)
		nntp_folder->search = camel_folder_search_new ();

	camel_folder_search_set_folder (nntp_folder->search, folder);
	matches = camel_folder_search_search(nntp_folder->search, expression, NULL, error);

	CAMEL_NNTP_FOLDER_UNLOCK(nntp_folder, search_lock);

	return matches;
}

static guint32
nntp_folder_count_by_expression (CamelFolder *folder, const gchar *expression, GError **error)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (folder);
	guint32 count;

	CAMEL_NNTP_FOLDER_LOCK(nntp_folder, search_lock);

	if (nntp_folder->search == NULL)
		nntp_folder->search = camel_folder_search_new ();

	camel_folder_search_set_folder (nntp_folder->search, folder);
	count = camel_folder_search_count(nntp_folder->search, expression, error);

	CAMEL_NNTP_FOLDER_UNLOCK(nntp_folder, search_lock);

	return count;
}

static GPtrArray *
nntp_folder_search_by_uids (CamelFolder *folder, const gchar *expression, GPtrArray *uids, GError **error)
{
	CamelNNTPFolder *nntp_folder = (CamelNNTPFolder *) folder;
	GPtrArray *matches;

	if (uids->len == 0)
		return g_ptr_array_new();

	CAMEL_NNTP_FOLDER_LOCK(folder, search_lock);

	if (nntp_folder->search == NULL)
		nntp_folder->search = camel_folder_search_new ();

	camel_folder_search_set_folder (nntp_folder->search, folder);
	matches = camel_folder_search_search(nntp_folder->search, expression, uids, error);

	CAMEL_NNTP_FOLDER_UNLOCK(folder, search_lock);

	return matches;
}

static void
nntp_folder_search_free (CamelFolder *folder, GPtrArray *result)
{
	CamelNNTPFolder *nntp_folder = CAMEL_NNTP_FOLDER (folder);

	CAMEL_NNTP_FOLDER_LOCK(nntp_folder, search_lock);
	camel_folder_search_free_result (nntp_folder->search, result);
	CAMEL_NNTP_FOLDER_UNLOCK(nntp_folder, search_lock);
}

static gboolean
nntp_folder_append_message_online (CamelFolder *folder,
                                   CamelMimeMessage *mime_message,
                                   const CamelMessageInfo *info,
                                   gchar **appended_uid,
                                   GError **error)
{
	CamelStore *parent_store;
	CamelNNTPStore *nntp_store;
	CamelStream *filtered_stream;
	CamelStream *stream;
	CamelMimeFilter *crlffilter;
	gint ret;
	guint u;
	struct _camel_header_raw *header, *savedhdrs, *n, *tail;
	const gchar *full_name;
	gchar *group, *line;
	gboolean success = TRUE;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	nntp_store = CAMEL_NNTP_STORE (parent_store);
	stream = CAMEL_STREAM (nntp_store->stream);

	camel_service_lock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	/* send 'POST' command */
	ret = camel_nntp_command (nntp_store, error, NULL, &line, "post");
	if (ret != 340) {
		if (ret == 440) {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INSUFFICIENT_PERMISSION,
				_("Posting failed: %s"), line);
			success = FALSE;
		} else if (ret != -1) {
			g_set_error (
				error, CAMEL_ERROR,
				CAMEL_ERROR_GENERIC,
				_("Posting failed: %s"), line);
			success = FALSE;
		}
		camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		return success;
	}

	/* the 'Newsgroups: ' header */
	group = g_strdup_printf ("Newsgroups: %s\r\n", full_name);

	/* setup stream filtering */
	crlffilter = camel_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_ENCODE, CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS);
	filtered_stream = camel_stream_filter_new (stream);
	camel_stream_filter_add (
		CAMEL_STREAM_FILTER (filtered_stream), crlffilter);
	g_object_unref (crlffilter);

	/* remove mail 'To', 'CC', and 'BCC' headers */
	savedhdrs = NULL;
	tail = (struct _camel_header_raw *) &savedhdrs;

	header = (struct _camel_header_raw *) &CAMEL_MIME_PART (mime_message)->headers;
	n = header->next;
	while (n != NULL) {
		if (!g_ascii_strcasecmp (n->name, "To") || !g_ascii_strcasecmp (n->name, "Cc") || !g_ascii_strcasecmp (n->name, "Bcc")) {
			header->next = n->next;
			tail->next = n;
			n->next = NULL;
			tail = n;
		} else {
			header = n;
		}

		n = header->next;
	}

	/* write the message */
	if (camel_stream_write(stream, group, strlen(group), error) == -1
	    || camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (mime_message), filtered_stream, error) == -1
	    || camel_stream_flush (filtered_stream, error) == -1
	    || camel_stream_write (stream, "\r\n.\r\n", 5, error) == -1
	    || (ret = camel_nntp_stream_line (nntp_store->stream, (guchar **)&line, &u, error)) == -1) {
		g_prefix_error (error, _("Posting failed: "));
		success = FALSE;
	} else if (atoi(line) != 240) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Posting failed: %s"), line);
		success = FALSE;
	}

	g_object_unref (filtered_stream);
	g_free(group);
	header->next = savedhdrs;

	camel_service_unlock (CAMEL_SERVICE (nntp_store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	return success;
}

static gboolean
nntp_folder_append_message_offline (CamelFolder *folder,
                                    CamelMimeMessage *mime_message,
                                    const CamelMessageInfo *info,
                                    gchar **appended_uid,
                                    GError **error)
{
	g_set_error (
		error, CAMEL_SERVICE_ERROR,
		CAMEL_SERVICE_ERROR_UNAVAILABLE,
		_("You cannot post NNTP messages while working offline!"));

	return FALSE;
}

/* I do not know what to do this exactly. Looking at the IMAP implementation for this, it
   seems to assume the message is copied to a folder on the same store. In that case, an
   NNTP implementation doesn't seem to make any sense. */
static gboolean
nntp_folder_transfer_message (CamelFolder *source,
                              GPtrArray *uids,
                              CamelFolder *dest,
                              GPtrArray **transferred_uids,
                              gboolean delete_orig,
                              GError **error)
{
	g_set_error (
		error, CAMEL_SERVICE_ERROR,
		CAMEL_SERVICE_ERROR_UNAVAILABLE,
		_("You cannot copy messages from a NNTP folder!"));

	return FALSE;
}

static gboolean
nntp_folder_expunge_uids_offline (CamelFolder *folder, GPtrArray *uids, GError **error)
{
	CamelFolderChangeInfo *changes;
	gint ii;

	g_return_val_if_fail (folder != NULL, FALSE);
	g_return_val_if_fail (CAMEL_IS_NNTP_FOLDER (folder), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);
	g_return_val_if_fail (folder->summary != NULL, FALSE);

	/* can only remove deleted messages from a local cache */

	changes = camel_folder_change_info_new ();
	for (ii = 0; ii < uids->len; ii++) {
		camel_folder_summary_remove_uid (folder->summary, uids->pdata[ii]);
		camel_folder_change_info_remove_uid (changes, uids->pdata[ii]);
	}

	camel_folder_summary_save_to_db (folder->summary, NULL);
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	return TRUE;
}

static void
camel_nntp_folder_class_init (CamelNNTPFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;
	CamelDiscoFolderClass *disco_folder_class;

	g_type_class_add_private (class, sizeof (CamelNNTPFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = nntp_folder_dispose;
	object_class->finalize = nntp_folder_finalize;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->get_message = nntp_folder_get_message;
	folder_class->search_by_expression = nntp_folder_search_by_expression;
	folder_class->count_by_expression = nntp_folder_count_by_expression;
	folder_class->search_by_uids = nntp_folder_search_by_uids;
	folder_class->search_free = nntp_folder_search_free;
	folder_class->get_filename = nntp_get_filename;

	disco_folder_class = CAMEL_DISCO_FOLDER_CLASS (class);
	disco_folder_class->sync_online = nntp_folder_sync_online;
	disco_folder_class->sync_resyncing = nntp_folder_sync_offline;
	disco_folder_class->sync_offline = nntp_folder_sync_offline;
	disco_folder_class->cache_message = nntp_folder_cache_message;
	disco_folder_class->append_online = nntp_folder_append_message_online;
	disco_folder_class->append_resyncing = nntp_folder_append_message_online;
	disco_folder_class->append_offline = nntp_folder_append_message_offline;
	disco_folder_class->transfer_online = nntp_folder_transfer_message;
	disco_folder_class->transfer_resyncing = nntp_folder_transfer_message;
	disco_folder_class->transfer_offline = nntp_folder_transfer_message;
	disco_folder_class->refresh_info_online = nntp_folder_refresh_info_online;
	disco_folder_class->expunge_uids_online = nntp_folder_expunge_uids_offline;
	disco_folder_class->expunge_uids_offline = nntp_folder_expunge_uids_offline;
	disco_folder_class->expunge_uids_resyncing = nntp_folder_expunge_uids_offline;
}

static void
camel_nntp_folder_init (CamelNNTPFolder *nntp_folder)
{
	nntp_folder->priv = CAMEL_NNTP_FOLDER_GET_PRIVATE (nntp_folder);

	nntp_folder->changes = camel_folder_change_info_new ();
	nntp_folder->priv->search_lock = g_mutex_new ();
	nntp_folder->priv->cache_lock = g_mutex_new ();
}

CamelFolder *
camel_nntp_folder_new (CamelStore *parent,
                       const gchar *folder_name,
                       GError **error)
{
	CamelFolder *folder;
	CamelNNTPFolder *nntp_folder;
	gchar *root;
	CamelService *service;
	CamelStoreInfo *si;
	gboolean subscribed = TRUE;

	service = (CamelService *) parent;
	root = camel_session_get_storage_path (service->session, service, error);
	if (root == NULL)
		return NULL;

	/* If this doesn't work, stuff wont save, but let it continue anyway */
	g_mkdir_with_parents (root, 0700);

	folder = g_object_new (
		CAMEL_TYPE_NNTP_FOLDER,
		"name", folder_name,
		"full-name", folder_name,
		"parent-store", parent, NULL);
	nntp_folder = (CamelNNTPFolder *)folder;

	folder->folder_flags |= CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY|CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;

	nntp_folder->storage_path = g_build_filename (root, folder_name, NULL);
	g_free (root);

	root = g_strdup_printf ("%s.cmeta", nntp_folder->storage_path);
	camel_object_set_state_filename (CAMEL_OBJECT (nntp_folder), root);
	camel_object_state_read (CAMEL_OBJECT (nntp_folder));
	g_free(root);

	root = g_strdup_printf("%s.ev-summary", nntp_folder->storage_path);
	folder->summary = (CamelFolderSummary *) camel_nntp_summary_new (folder, root);
	g_free(root);

	camel_folder_summary_load_from_db (folder->summary, NULL);

	si = camel_store_summary_path ((CamelStoreSummary *) ((CamelNNTPStore*) parent)->summary, folder_name);
	if (si) {
		subscribed = (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) != 0;
		camel_store_summary_info_free ((CamelStoreSummary *) ((CamelNNTPStore*) parent)->summary, si);
	}

	if (subscribed && !camel_folder_refresh_info (folder, error)) {
		g_object_unref (folder);
		folder = NULL;
        }

	return folder;
}

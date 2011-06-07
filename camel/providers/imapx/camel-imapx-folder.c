/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.c : class for a imap folder */
/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <glib/gi18n-lib.h>

#include "camel-imapx-utils.h"
#include "camel-imapx-store.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-server.h"

#include <stdlib.h>
#include <string.h>

#define d(x) camel_imapx_debug(debug, x)

G_DEFINE_TYPE (CamelIMAPXFolder, camel_imapx_folder, CAMEL_TYPE_OFFLINE_FOLDER)

CamelFolder *
camel_imapx_folder_new(CamelStore *store, const gchar *folder_dir, const gchar *folder_name, GError **error)
{
	CamelFolder *folder;
	CamelIMAPXFolder *ifolder;
	const gchar *short_name;
	gchar *summary_file, *state_file;
	CamelIMAPXStore *istore;

	d(printf("opening imap folder '%s'\n", folder_dir));

	short_name = strrchr (folder_name, '/');
	if (short_name)
		short_name++;
	else
		short_name = folder_name;

	folder = g_object_new (
		CAMEL_TYPE_IMAPX_FOLDER,
		"name", short_name,
		"full_name", folder_name,
		"parent-store", store, NULL);
	ifolder = (CamelIMAPXFolder *) folder;

	((CamelIMAPXFolder *)folder)->raw_name = g_strdup(folder_name);

	summary_file = g_strdup_printf ("%s/summary", folder_dir);
	folder->summary = camel_imapx_summary_new(folder, summary_file);
	if (!folder->summary) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not create folder summary for %s"),
			short_name);
		return NULL;
	}

	ifolder->cache = camel_data_cache_new (folder_dir, error);
	if (!ifolder->cache) {
		g_prefix_error (
			error, _("Could not create cache for %s: "),
			short_name);
		return NULL;
	}

	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	camel_object_set_state_filename (CAMEL_OBJECT (folder), state_file);
	g_free(state_file);
	camel_object_state_read (CAMEL_OBJECT (folder));

	ifolder->search = camel_folder_search_new ();
	ifolder->search_lock = g_mutex_new ();
	ifolder->stream_lock = g_mutex_new ();
	ifolder->ignore_recent = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);
	ifolder->exists_on_server = 0;
	ifolder->unread_on_server = 0;
	ifolder->modseq_on_server = 0;
	ifolder->uidnext_on_server = 0;

	istore = (CamelIMAPXStore *) store;
	if (!g_ascii_strcasecmp (folder_name, "INBOX")) {
		if ((istore->rec_options & IMAPX_FILTER_INBOX))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
		if ((istore->rec_options & IMAPX_FILTER_INBOX))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
	} else if ((istore->rec_options & (IMAPX_FILTER_JUNK | IMAPX_FILTER_JUNK_INBOX)) == IMAPX_FILTER_JUNK)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;

	g_free (summary_file);

	return folder;
}

static void
imapx_folder_dispose (GObject *object)
{
	CamelIMAPXFolder *folder = CAMEL_IMAPX_FOLDER (object);

	if (folder->cache != NULL) {
		g_object_unref (folder->cache);
		folder->cache = NULL;
	}

	if (folder->search != NULL) {
		g_object_unref (folder->search);
		folder->search = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_folder_parent_class)->dispose (object);
}

static void
imapx_folder_finalize (GObject *object)
{
	CamelIMAPXFolder *folder = CAMEL_IMAPX_FOLDER (object);

	if (folder->ignore_recent != NULL)
		g_hash_table_unref (folder->ignore_recent);

	g_mutex_free (folder->search_lock);
	g_mutex_free (folder->stream_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_folder_parent_class)->finalize (object);
}

static gboolean
imapx_refresh_info (CamelFolder *folder, GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	gboolean success = FALSE;

	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (!camel_service_connect((CamelService *)istore, error))
		return FALSE;

	server = camel_imapx_store_get_server(istore, camel_folder_get_full_name (folder), error);
	if (server != NULL) {
		success = camel_imapx_server_refresh_info(server, folder, error);
		camel_imapx_store_op_done (istore, server, camel_folder_get_full_name (folder));
		g_object_unref(server);
	}

	return success;
}

static gboolean
imapx_expunge (CamelFolder *folder, GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;

	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	server = camel_imapx_store_get_server(istore, camel_folder_get_full_name (folder), error);
	if (server) {
		camel_imapx_server_expunge(server, folder, error);
		camel_imapx_store_op_done (istore, server, camel_folder_get_full_name (folder));
		g_object_unref(server);
		return TRUE;
	}

	return FALSE;
}

static gboolean
imapx_sync (CamelFolder *folder, gboolean expunge, GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;

	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	server = camel_imapx_store_get_server (istore, camel_folder_get_full_name (folder), error);
	if (!server)
		return FALSE;

	camel_imapx_server_sync_changes (server, folder, NULL);

	/* Sync twice - make sure deleted flags are written out,
	   then sync again incase expunge changed anything */

	if (expunge)
		camel_imapx_server_expunge (server, folder, NULL);

	camel_imapx_store_op_done (istore, server, camel_folder_get_full_name (folder));
	g_object_unref (server);

	return TRUE;
}

static CamelMimeMessage *
imapx_get_message (CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelMimeMessage *msg = NULL;
	CamelStream *stream = NULL;
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelIMAPXServer *server;
	const gchar *path = NULL;
	gboolean offline_message = FALSE;

	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (!strchr (uid, '-'))
		path = "cur";
	else {
		path = "new";
		offline_message = TRUE;
	}

	stream = camel_data_cache_get (ifolder->cache, path, uid, NULL);
	if (!stream) {
		if (offline_message) {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID_UID,
				"Offline message vanished from disk: %s", uid);
			return NULL;
		}

		if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("You must be working online to complete this operation"));
			return NULL;
		}

		server = camel_imapx_store_get_server(istore, camel_folder_get_full_name (folder), error);
		if (server) {
			stream = camel_imapx_server_get_message(server, folder, uid, error);
			camel_imapx_store_op_done (istore, server, camel_folder_get_full_name (folder));
			g_object_unref(server);
		} else
			return NULL;
	}

	if (stream != NULL) {
		msg = camel_mime_message_new();

		g_mutex_lock (ifolder->stream_lock);
		if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)msg, stream, error) == -1) {
			g_object_unref (msg);
			msg = NULL;
		}
		g_mutex_unlock (ifolder->stream_lock);
		g_object_unref (stream);
	}

	return msg;
}

static gboolean
imapx_sync_message (CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	gboolean success;

	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	server = camel_imapx_store_get_server (istore, camel_folder_get_full_name (folder), error);
	if (server == NULL)
		return FALSE;

	success = camel_imapx_server_sync_message (server, folder, uid, error);
	camel_imapx_store_op_done (istore, server, camel_folder_get_full_name (folder));
	g_object_unref(server);

	return success;
}

static gboolean
imapx_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
		      CamelFolder *dest, GPtrArray **transferred_uids,
		      gboolean delete_originals, GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	gboolean success = FALSE;

	parent_store = camel_folder_get_parent_store (source);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	server = camel_imapx_store_get_server (istore, camel_folder_get_full_name (source), error);
	if (server) {
		success = camel_imapx_server_copy_message (server, source, dest, uids, delete_originals, error);
		camel_imapx_store_op_done (istore, server, camel_folder_get_full_name (source));
		g_object_unref(server);
	}

	imapx_refresh_info (dest, NULL);

	return success;
}

static gboolean
imapx_append_message(CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, gchar **appended_uid, GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	gboolean success = FALSE;

	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (appended_uid)
		*appended_uid = NULL;

	server = camel_imapx_store_get_server (istore, NULL, error);
	if (server) {
		success = camel_imapx_server_append_message (server, folder, message, info, error);
		g_object_unref(server);
	}

	return success;
}

gchar *
imapx_get_filename (CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;

	return camel_data_cache_get_filename (ifolder->cache, "cache", uid, error);
}

/* Algorithm for selecting a folder:

  - If uidvalidity == old uidvalidity
    and exsists == old exists
    and recent == old recent
    and unseen == old unseen
    Assume our summary is correct
  for each summary item
    mark the summary item as 'old/not updated'
  rof
  fetch flags from 1:*
  for each fetch response
    info = summary[index]
    if info.uid != uid
      info = summary_by_uid[uid]
    fi
    if info == NULL
      create new info @ index
    fi
    if got.flags
      update flags
    fi
    if got.header
      update based on header
      mark as retrieved
    else if got.body
      update based on imap body
      mark as retrieved
    fi

  Async fetch response:
    info = summary[index]
    if info == null
       if uid == null
          force resync/select?
       info = empty @ index
    else if uid && info.uid != uid
       force a resync?
       return
    fi

    if got.flags {
      info.flags = flags
    }
    if got.header {
      info.init(header)
      info.empty = false
    }

info.state - 2 bit field in flags
   0 = empty, nothing set
   1 = uid & flags set
   2 = update required
   3 = up to date
*/

static void
imapx_search_free (CamelFolder *folder, GPtrArray *uids)
{
	CamelIMAPXFolder *ifolder = CAMEL_IMAPX_FOLDER(folder);

	g_return_if_fail (ifolder->search);

	g_mutex_lock (ifolder->search_lock);

	camel_folder_search_free_result (ifolder->search, uids);

	g_mutex_unlock (ifolder->search_lock);
}

static GPtrArray *
imapx_search_by_uids (CamelFolder *folder, const gchar *expression, GPtrArray *uids, GError **error)
{
	CamelIMAPXFolder *ifolder = CAMEL_IMAPX_FOLDER(folder);
	GPtrArray *matches;

	if (uids->len == 0)
		return g_ptr_array_new();

	g_mutex_lock (ifolder->search_lock);

	camel_folder_search_set_folder(ifolder->search, folder);
	matches = camel_folder_search_search(ifolder->search, expression, uids, error);

	g_mutex_unlock (ifolder->search_lock);

	return matches;
}

static guint32
imapx_count_by_expression (CamelFolder *folder, const gchar *expression, GError **error)
{
	CamelIMAPXFolder *ifolder = CAMEL_IMAPX_FOLDER(folder);
	guint32 matches;

	g_mutex_lock (ifolder->search_lock);

	camel_folder_search_set_folder (ifolder->search, folder);
	matches = camel_folder_search_count (ifolder->search, expression, error);

	g_mutex_unlock (ifolder->search_lock);

	return matches;
}

static GPtrArray *
imapx_search_by_expression (CamelFolder *folder, const gchar *expression, GError **error)
{
	CamelIMAPXFolder *ifolder = CAMEL_IMAPX_FOLDER (folder);
	GPtrArray *matches;

	g_mutex_lock (ifolder->search_lock);

	camel_folder_search_set_folder (ifolder->search, folder);
	matches = camel_folder_search_search(ifolder->search, expression, NULL, error);

	g_mutex_unlock (ifolder->search_lock);

	return matches;
}

static gboolean
imapx_fetch_old_messages (CamelFolder *folder, int count, GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	gboolean ret = FALSE;

	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (!camel_service_connect((CamelService *)istore, error))
		return FALSE;

	server = camel_imapx_store_get_server(istore, camel_folder_get_full_name (folder), error);
	if (server != NULL) {
		ret = camel_imapx_server_fetch_old_messages (server, folder, count, error);
		camel_imapx_store_op_done (istore, server, camel_folder_get_full_name (folder));
		g_object_unref(server);
	}

	return ret;

}

static gboolean
imapx_purge_old_messages (CamelFolder *folder, GError **error)
{
	/* Not implemented now */
	return FALSE;
}

static void
camel_imapx_folder_class_init (CamelIMAPXFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = imapx_folder_dispose;
	object_class->finalize = imapx_folder_finalize;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->refresh_info = imapx_refresh_info;
	folder_class->sync = imapx_sync;
	folder_class->search_by_expression = imapx_search_by_expression;
	folder_class->search_by_uids = imapx_search_by_uids;
	folder_class->count_by_expression = imapx_count_by_expression;
	folder_class->search_free = imapx_search_free;
	folder_class->expunge = imapx_expunge;
	folder_class->get_message = imapx_get_message;
	folder_class->sync_message = imapx_sync_message;
	folder_class->append_message = imapx_append_message;
	folder_class->transfer_messages_to = imapx_transfer_messages_to;
	folder_class->get_filename = imapx_get_filename;
	folder_class->fetch_old_messages = imapx_fetch_old_messages;
	folder_class->purge_old_messages = imapx_purge_old_messages;
}

static void
camel_imapx_folder_init (CamelIMAPXFolder *imapx_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (imapx_folder);

	folder->folder_flags |= (CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY |
				 CAMEL_FOLDER_HAS_SEARCH_CAPABILITY);

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_USER;

	camel_folder_set_lock_async (folder, TRUE);
}


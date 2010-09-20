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

#include "camel/camel-exception.h"
#include "camel/camel-stream-mem.h"
#include "camel/camel-stream-filter.h"
#include "camel/camel-mime-message.h"
#include "camel/camel-operation.h"
#include "camel/camel-data-cache.h"
#include "camel/camel-session.h"
#include "camel/camel-file-utils.h"
#include  "camel/camel-string-utils.h"
#include "camel-folder-search.h"

#include "camel-imapx-utils.h"
#include "camel-imapx-store.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-exception.h"
#include "camel-imapx-server.h"

#include <libedataserver/md5-utils.h>

#include <stdlib.h>
#include <string.h>

#define d(x) camel_imapx_debug(debug, x)

#define CF_CLASS(o) (CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(o)))
static CamelObjectClass *parent_class;
static CamelOfflineFolderClass *offline_folder_class = NULL;

CamelFolder *
camel_imapx_folder_new(CamelStore *store, const gchar *folder_dir, const gchar *folder_name, CamelException *ex)
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

	folder = CAMEL_FOLDER (camel_object_new (CAMEL_IMAPX_FOLDER_TYPE));
	camel_folder_construct(folder, store, folder_name, short_name);
	ifolder = (CamelIMAPXFolder *) folder;

	((CamelIMAPXFolder *)folder)->raw_name = g_strdup(folder_name);

	summary_file = g_strdup_printf ("%s/summary", folder_dir);
	folder->summary = camel_imapx_summary_new(folder, summary_file);
	if (!folder->summary) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				_("Could not create folder summary for %s"),
				short_name);
		return NULL;
	}

	ifolder->cache = camel_data_cache_new (folder_dir, 0, ex);
	if (!ifolder->cache) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not create cache for %s"),
				      short_name);
		return NULL;
	}

	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state_file, NULL);
	g_free(state_file);
	camel_object_state_read(folder);

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
imapx_refresh_info (CamelFolder *folder, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)folder->parent_store;
	CamelIMAPXServer *server;

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	camel_service_connect((CamelService *)istore, ex);
	server = camel_imapx_store_get_server(istore, ex);
	if (server) {
		camel_imapx_server_refresh_info(server, folder, ex);
		camel_object_unref(server);
	}
}

static void
imapx_expunge (CamelFolder *folder, CamelException *ex)
{
	CamelIMAPXStore *is = (CamelIMAPXStore *)folder->parent_store;
	CamelIMAPXServer *server;

	if (CAMEL_OFFLINE_STORE (is)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	server = camel_imapx_store_get_server(is, ex);
	if (server) {
		camel_imapx_server_expunge(server, folder, ex);
		camel_object_unref(server);
	}

}

static void
imapx_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelIMAPXStore *is = (CamelIMAPXStore *)folder->parent_store;
	CamelIMAPXServer *server;
	CamelException eex = CAMEL_EXCEPTION_INITIALISER;

	if (CAMEL_OFFLINE_STORE (is)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	if (!ex)
		ex = &eex;

	server = camel_imapx_store_get_server(is, ex);
	if (!server)
		return;

	camel_imapx_server_sync_changes (server, folder, ex);

	/* Sync twice - make sure deleted flags are written out,
	   then sync again incase expunge changed anything */
	camel_exception_clear(ex);

	if (expunge) {
		camel_imapx_server_expunge(server, folder, ex);
		camel_exception_clear(ex);
	}
	camel_object_unref(server);
}

static CamelMimeMessage *
imapx_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelMimeMessage *msg = NULL;
	CamelStream *stream = NULL;
	CamelIMAPXStore *istore = (CamelIMAPXStore *)folder->parent_store;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelIMAPXServer *server;
	const gchar *path = NULL;
	gboolean offline_message = FALSE;

	if (!strchr (uid, '-'))
		path = "cur";
	else {
		path = "new";
		offline_message = TRUE;
	}

	stream = camel_data_cache_get (ifolder->cache, path, uid, NULL);
	if (!stream) {
		if (offline_message) {
			camel_exception_setv(ex, 2, "Offline message vanished from disk: %s", uid);
			return NULL;
		}

		if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
			return NULL;

		server = camel_imapx_store_get_server(istore, ex);
		if (server) {
			stream = camel_imapx_server_get_message(server, folder, uid, ex);
			camel_object_unref(server);
		} else {
			/* It should _always_ be set */
			if (!camel_exception_is_set (ex))
				camel_exception_setv(ex, 1, "not authenticated");
			return NULL;
		}
	}

	if (!camel_exception_is_set (ex) && stream) {
		msg = camel_mime_message_new();

		g_mutex_lock (ifolder->stream_lock);
		if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)msg, stream) == -1) {
			camel_object_unref(msg);
			msg = NULL;
		}
		g_mutex_unlock (ifolder->stream_lock);
		camel_object_unref(stream);
	}

	return msg;
}

static void
imapx_sync_message (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)folder->parent_store;
	CamelIMAPXServer *server;

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	server = camel_imapx_store_get_server(istore, ex);
	if (server) {
		camel_imapx_server_sync_message (server, folder, uid, ex);
		camel_object_unref(server);
	}
}

static void
imapx_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
		      CamelFolder *dest, GPtrArray **transferred_uids,
		      gboolean delete_originals, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) source->parent_store;
	CamelIMAPXServer *server;

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	server = camel_imapx_store_get_server(istore, ex);
	if (server) {
		camel_imapx_server_copy_message (server, source, dest, uids, delete_originals, ex);
		camel_object_unref(server);
	}

	imapx_refresh_info (dest, ex);
}

static void
imapx_append_message(CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, gchar **appended_uid, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)folder->parent_store;
	CamelIMAPXServer *server;

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	if (appended_uid)
		*appended_uid = NULL;

	server = camel_imapx_store_get_server(istore, ex);
	if (server) {
		camel_imapx_server_append_message(server, folder, message, info, ex);
		camel_object_unref(server);
	}
}

gchar *
imapx_get_filename (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;

	return camel_data_cache_get_filename (ifolder->cache, "cache", uid, NULL);
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
imapx_search_by_uids (CamelFolder *folder, const gchar *expression, GPtrArray *uids, CamelException *ex)
{
	CamelIMAPXFolder *ifolder = CAMEL_IMAPX_FOLDER(folder);
	GPtrArray *matches;

	if (uids->len == 0)
		return g_ptr_array_new();

	g_mutex_lock (ifolder->search_lock);

	camel_folder_search_set_folder(ifolder->search, folder);
	matches = camel_folder_search_search(ifolder->search, expression, uids, ex);

	g_mutex_unlock (ifolder->search_lock);

	return matches;
}

static guint32
imapx_count_by_expression (CamelFolder *folder, const gchar *expression, CamelException *ex)
{
	CamelIMAPXFolder *ifolder = CAMEL_IMAPX_FOLDER(folder);
	guint32 matches;

	g_mutex_lock (ifolder->search_lock);

	camel_folder_search_set_folder (ifolder->search, folder);
	matches = camel_folder_search_count (ifolder->search, expression, ex);

	g_mutex_unlock (ifolder->search_lock);

	return matches;
}

static GPtrArray *
imapx_search_by_expression (CamelFolder *folder, const gchar *expression, CamelException *ex)
{
	CamelIMAPXFolder *ifolder = CAMEL_IMAPX_FOLDER (folder);
	GPtrArray *matches;

	g_mutex_lock (ifolder->search_lock);

	camel_folder_search_set_folder (ifolder->search, folder);
	matches = camel_folder_search_search(ifolder->search, expression, NULL, ex);

	g_mutex_unlock (ifolder->search_lock);

	return matches;
}

static void
imapx_folder_class_init (CamelIMAPXFolderClass *klass)
{
	offline_folder_class = CAMEL_OFFLINE_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_offline_folder_get_type ()));

	((CamelFolderClass *)klass)->refresh_info = imapx_refresh_info;
	((CamelFolderClass *)klass)->sync = imapx_sync;
	((CamelFolderClass *)klass)->search_by_expression = imapx_search_by_expression;
	((CamelFolderClass *)klass)->search_by_uids = imapx_search_by_uids;
	((CamelFolderClass *)klass)->count_by_expression = imapx_count_by_expression;
	((CamelFolderClass *)klass)->search_free = imapx_search_free;

	((CamelFolderClass *)klass)->expunge = imapx_expunge;
	((CamelFolderClass *)klass)->get_message = imapx_get_message;
	((CamelFolderClass *)klass)->sync_message = imapx_sync_message;
	((CamelFolderClass *)klass)->append_message = imapx_append_message;
	((CamelFolderClass *)klass)->transfer_messages_to = imapx_transfer_messages_to;
	((CamelFolderClass *)klass)->get_filename = imapx_get_filename;
}

static void
imapx_folder_init(CamelObject *o, CamelObjectClass *klass)
{
	CamelFolder *folder = (CamelFolder *)o;

	folder->folder_flags |= (CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY |
				 CAMEL_FOLDER_HAS_SEARCH_CAPABILITY);

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_USER;

	camel_folder_set_lock_async (folder, TRUE);
}

static void
imapx_finalize (CamelObject *object)
{
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) object;

	camel_object_unref (CAMEL_OBJECT (ifolder->cache));

	if (ifolder->ignore_recent)
		g_hash_table_unref (ifolder->ignore_recent);

	g_mutex_free (ifolder->search_lock);
	g_mutex_free (ifolder->stream_lock);
	if (ifolder->search)
		camel_object_unref (CAMEL_OBJECT (ifolder->search));
}

CamelType
camel_imapx_folder_get_type (void)
{
	static CamelType camel_imapx_folder_type = CAMEL_INVALID_TYPE;

	if (!camel_imapx_folder_type) {
		parent_class = camel_offline_folder_get_type();
		camel_imapx_folder_type = camel_type_register (parent_class, "CamelIMAPXFolder",
							      sizeof (CamelIMAPXFolder),
							      sizeof (CamelIMAPXFolderClass),
							      (CamelObjectClassInitFunc)imapx_folder_class_init,
							      NULL,
							      imapx_folder_init,
							      (CamelObjectFinalizeFunc) imapx_finalize);
	}

	return camel_imapx_folder_type;
}

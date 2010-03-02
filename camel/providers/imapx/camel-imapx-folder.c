/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.c : class for a imap folder */

/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2002 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
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

#include "camel-imapx-store.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-exception.h"
#include "camel-imapx-server.h"

#include <libedataserver/md5-utils.h>

#include <stdlib.h>
#include <string.h>

#define d(x)

#define CF_CLASS(o) (CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(o)))
static CamelFolderClass *parent_class;

CamelFolder *
camel_imapx_folder_new(CamelStore *store, const gchar *folder_dir, const gchar *folder_name, CamelException *ex)
{
	CamelFolder *folder;
	CamelIMAPXFolder *ifolder;
	const gchar *short_name;
	gchar *summary_file;

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

	ifolder->search = camel_folder_search_new ();
	ifolder->search_lock = g_mutex_new ();
	ifolder->exists_on_server = 0;
	ifolder->unread_on_server = 0;

	g_free (summary_file);

	return folder;
}

#if 0
/* experimental interfaces */
void
camel_imapx_folder_open(CamelIMAPXFolder *folder, CamelException *ex)
{
	/* */
}

void
camel_imapx_folder_delete(CamelIMAPXFolder *folder, CamelException *ex)
{
}

void
camel_imapx_folder_rename(CamelIMAPXFolder *folder, const gchar *new, CamelException *ex)
{
}

void
camel_imapx_folder_close(CamelIMAPXFolder *folder, CamelException *ex)
{
}
#endif

static void
imapx_refresh_info (CamelFolder *folder, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)folder->parent_store;

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	if (istore->server && camel_imapx_server_connect (istore->server, 1))
		camel_imapx_server_refresh_info(istore->server, folder, ex);

}

static void 
imapx_expunge (CamelFolder *folder, CamelException *ex)
{
	CamelIMAPXStore *is = (CamelIMAPXStore *)folder->parent_store;

	if (CAMEL_OFFLINE_STORE (is)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	if (is->server && camel_imapx_server_connect (is->server, 1))
		camel_imapx_server_expunge(is->server, folder, ex);

}

static void
imapx_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelIMAPXStore *is = (CamelIMAPXStore *)folder->parent_store;

	if (CAMEL_OFFLINE_STORE (is)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	if (is->server && camel_imapx_server_connect (is->server, 1))
		camel_imapx_server_sync_changes (is->server, folder, ex);

	/* Sync twice - make sure deleted flags are written out,
	   then sync again incase expunge changed anything */
	camel_exception_clear(ex);

	if (is->server && expunge) {
		camel_imapx_server_expunge(is->server, folder, ex);
		camel_exception_clear(ex);
	}
}

static CamelMimeMessage *
imapx_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelMimeMessage *msg = NULL;
	CamelStream *stream = NULL;
	CamelIMAPXStore *istore = (CamelIMAPXStore *)folder->parent_store;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
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

		if (istore->server && camel_imapx_server_connect (istore->server, 1)) {
			stream = camel_imapx_server_get_message(istore->server, folder, uid, ex);
		} else {
			camel_exception_setv(ex, 1, "not authenticated");
			return NULL;
		}
	}

	if (!camel_exception_is_set (ex) && stream) {
		msg = camel_mime_message_new();
		if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)msg, stream) == -1) {
			camel_object_unref(msg);
			msg = NULL;
		}
		camel_object_unref(stream);
	}

	return msg;
}

static void
imapx_sync_message (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)folder->parent_store;
	
	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	if (istore->server && camel_imapx_server_connect (istore->server, 1))
		camel_imapx_server_sync_message (istore->server, folder, uid, ex);
}

static void
imapx_transfer_messages_to (CamelFolder *source, GPtrArray *uids,
		      CamelFolder *dest, GPtrArray **transferred_uids,
		      gboolean delete_originals, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *) source->parent_store;

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	if (istore->server && camel_imapx_server_connect (istore->server, 1))
		camel_imapx_server_copy_message (istore->server, source, dest, uids, delete_originals, ex);

	imapx_refresh_info (dest, ex);
}

static void
imapx_append_message(CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, gchar **appended_uid, CamelException *ex)
{
	CamelIMAPXStore *istore = (CamelIMAPXStore *)folder->parent_store;

	if (CAMEL_OFFLINE_STORE (istore)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	if (appended_uid)
		*appended_uid = NULL;

	if (istore->server && camel_imapx_server_connect (istore->server, 1))
		camel_imapx_server_append_message(istore->server, folder, message, info, ex);
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
imap_folder_class_init (CamelIMAPXFolderClass *klass)
{
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
imap_folder_init(CamelObject *o, CamelObjectClass *klass)
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
imap_finalize (CamelObject *object)
{
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) object;

	camel_object_unref (CAMEL_OBJECT (ifolder->cache));

	g_mutex_free (ifolder->search_lock);
	if (ifolder->search)
		camel_object_unref (CAMEL_OBJECT (ifolder->search));
}

CamelType
camel_imapx_folder_get_type (void)
{
	static CamelType camel_imapx_folder_type = CAMEL_INVALID_TYPE;

	if (!camel_imapx_folder_type) {
		camel_imapx_folder_type = camel_type_register (CAMEL_FOLDER_TYPE, "CamelIMAPXFolder",
							      sizeof (CamelIMAPXFolder),
							      sizeof (CamelIMAPXFolderClass),
							      (CamelObjectClassInitFunc)imap_folder_class_init,
							      NULL,
							      imap_folder_init,
							      (CamelObjectFinalizeFunc) imap_finalize);
		parent_class = (CamelFolderClass *)camel_folder_get_type();
	}

	return camel_imapx_folder_type;
}

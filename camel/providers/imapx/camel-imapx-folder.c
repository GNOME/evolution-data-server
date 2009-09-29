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

#include "camel/camel-exception.h"
#include "camel/camel-stream-mem.h"
#include "camel/camel-stream-filter.h"
#include "camel/camel-mime-message.h"
#include "camel/camel-operation.h"
#include "camel/camel-data-cache.h"
#include "camel/camel-session.h"
#include "camel/camel-file-utils.h"

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
camel_imapx_folder_new(CamelStore *store, const char *path, const char *raw)
{
	CamelFolder *folder;

	d(printf("opening imap folder '%s'\n", path));
	
	folder = CAMEL_FOLDER (camel_object_new (CAMEL_IMAPX_FOLDER_TYPE));
	camel_folder_construct(folder, store, path, path);

	((CamelIMAPXFolder *)folder)->raw_name = g_strdup(raw);

	folder->summary = camel_imapx_summary_new(folder, raw);

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
camel_imapx_folder_rename(CamelIMAPXFolder *folder, const char *new, CamelException *ex)
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
	CamelIMAPXStore *is = (CamelIMAPXStore *)folder->parent_store;

	if (is->server)
		camel_imapx_server_refresh_info(is->server, folder, ex);

}

static void
imapx_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelIMAPXStore *is = (CamelIMAPXStore *)folder->parent_store;

	/* Sync twice - make sure deleted flags are written out,
	   then sync again incase expunge changed anything */
	camel_exception_clear(ex);

	if (is->server && expunge) {
		camel_imapx_server_expunge(is->server, folder, ex);
		camel_exception_clear(ex);
	}

}

static CamelMimeMessage *
imapx_get_message (CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelMimeMessage *msg = NULL;
	CamelStream *stream;
	CamelIMAPXStore *is = (CamelIMAPXStore *)folder->parent_store;

	if (is->server) {
		stream = camel_imapx_server_get_message(is->server, folder, uid, ex);
		if (stream) {
			msg = camel_mime_message_new();
			if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)msg, stream) == -1) {
				camel_exception_setv(ex, 1, "error building message?");
				camel_object_unref(msg);
				msg = NULL;
			}
			camel_object_unref(stream);
		}
	} else {
		camel_exception_setv(ex, 1, "not ready");
	}

	return msg;
}

static void
imapx_append_message(CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, char **appended_uid, CamelException *ex)
{
	CamelIMAPXStore *is = (CamelIMAPXStore *)folder->parent_store;

	if (appended_uid)
		*appended_uid = NULL;

	camel_imapx_server_append_message(is->server, folder, message, info, ex);
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
imap_folder_class_init (CamelIMAPXFolderClass *klass)
{
	((CamelFolderClass *)klass)->refresh_info = imapx_refresh_info;
	((CamelFolderClass *)klass)->sync = imapx_sync;
	
	((CamelFolderClass *)klass)->get_message = imapx_get_message;
	((CamelFolderClass *)klass)->append_message = imapx_append_message;
}

static void
imap_folder_init(CamelObject *o, CamelObjectClass *klass)
{
	CamelFolder *folder = (CamelFolder *)o;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *)o;

	folder->folder_flags |= (CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY |
				 CAMEL_FOLDER_HAS_SEARCH_CAPABILITY);
	
	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_USER;

}

static void
imap_finalise(CamelObject *object)
{
	CamelIMAPXFolder *folder = (CamelIMAPXFolder *)object;

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
							      (CamelObjectFinalizeFunc)imap_finalise);
		parent_class = (CamelFolderClass *)camel_folder_get_type();
	}
	
	return camel_imapx_folder_type;
}

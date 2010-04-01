/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.c: class for an imap folder */

/*
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Jeffrey Stedfast <fejj@ximian.com>
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

#include <config.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include <libedataserver/e-time-utils.h>

#include "camel-db.h"
#include "camel-data-wrapper.h"
#include "camel-debug.h"
#include "camel-imap-journal.h"
#include "camel-exception.h"
#include "camel-file-utils.h"
#include "camel-mime-filter-crlf.h"
#include "camel-mime-filter-from.h"
#include "camel-mime-message.h"
#include "camel-mime-utils.h"
#include "camel-mime-part-utils.h"
#include "camel-multipart-encrypted.h"
#include "camel-multipart-signed.h"
#include "camel-multipart.h"
#include "camel-operation.h"
#include "camel-private.h"
#include "camel-session.h"
#include "camel-store-summary.h"
#include "camel-stream-buffer.h"
#include "camel-stream-filter.h"
#include "camel-stream-mem.h"
#include "camel-stream.h"
#include "camel-string-utils.h"

#include "camel-imap-command.h"
#include "camel-imap-folder.h"
#include "camel-imap-message-cache.h"
#include "camel-imap-private.h"
#include "camel-imap-search.h"
#include "camel-imap-store.h"
#include "camel-imap-store-summary.h"
#include "camel-imap-summary.h"
#include "camel-imap-utils.h"
#include "camel-imap-wrapper.h"

#define d(x)

/* set to -1 for infinite size (suggested max command-line length is
 * 1000 octets (see rfc2683), so we should keep the uid-set length to
 * something under that so that our command-lines don't exceed 1000
 * octets) */
#define UID_SET_LIMIT  (768)

extern gint camel_application_is_exiting;

#define CF_CLASS(o) (CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(o)))
static CamelOfflineFolderClass *offline_folder_class = NULL;

static CamelProperty imap_property_list[] = {
	{ CAMEL_IMAP_FOLDER_CHECK_FOLDER, "check_folder", N_("Always check for new mail in this folder") },
};

static void imap_finalize (CamelObject *object);
static gint imap_getv (CamelObject *object, CamelException *ex, CamelArgGetV *args);
static gint imap_setv (CamelObject *object, CamelException *ex, CamelArgV *args);

static void imap_rescan (CamelFolder *folder, gint exists, CamelException *ex);
static void imap_refresh_info (CamelFolder *folder, CamelException *ex);
static void imap_sync_offline (CamelFolder *folder, CamelException *ex);
static void imap_sync (CamelFolder *folder, gboolean expunge, CamelException *ex);
static void imap_expunge_uids_online (CamelFolder *folder, GPtrArray *uids, CamelException *ex);
static void imap_expunge_uids_offline (CamelFolder *folder, GPtrArray *uids, CamelException *ex);
static void imap_expunge (CamelFolder *folder, CamelException *ex);
/*static void imap_cache_message (CamelDiscoFolder *disco_folder, const gchar *uid, CamelException *ex);*/
static void imap_rename (CamelFolder *folder, const gchar *new);
static GPtrArray * imap_get_uncached_uids (CamelFolder *folder, GPtrArray * uids, CamelException *ex);
static gchar * imap_get_filename (CamelFolder *folder, const gchar *uid, CamelException *ex);

/* message manipulation */
static CamelMimeMessage *imap_get_message (CamelFolder *folder, const gchar *uid,
					   CamelException *ex);
static void imap_sync_message (CamelFolder *folder, const gchar *uid,
			       CamelException *ex);
static void imap_append_online (CamelFolder *folder, CamelMimeMessage *message,
				const CamelMessageInfo *info, gchar **appended_uid,
				CamelException *ex);
static void imap_append_offline (CamelFolder *folder, CamelMimeMessage *message,
				 const CamelMessageInfo *info, gchar **appended_uid,
				 CamelException *ex);

static void imap_transfer_online (CamelFolder *source, GPtrArray *uids,
				  CamelFolder *dest, GPtrArray **transferred_uids,
				  gboolean delete_originals,
				  CamelException *ex);
static void imap_transfer_offline (CamelFolder *source, GPtrArray *uids,
				   CamelFolder *dest, GPtrArray **transferred_uids,
				   gboolean delete_originals,
				   CamelException *ex);

/* searching */
static GPtrArray *imap_search_by_expression (CamelFolder *folder, const gchar *expression, CamelException *ex);
static guint32 imap_count_by_expression (CamelFolder *folder, const gchar *expression, CamelException *ex);
static GPtrArray *imap_search_by_uids	    (CamelFolder *folder, const gchar *expression, GPtrArray *uids, CamelException *ex);
static void       imap_search_free          (CamelFolder *folder, GPtrArray *uids);

static void imap_thaw (CamelFolder *folder);
static CamelFolderQuotaInfo *imap_get_quota_info (CamelFolder *folder);

static CamelObjectClass *parent_class;

static GData *parse_fetch_response (CamelImapFolder *imap_folder, gchar *msg_att);

/* internal helpers */
static CamelImapMessageInfo * imap_folder_summary_uid_or_error(
	CamelFolderSummary *summary,
	const gchar * uid,
	CamelException *ex);

#ifdef G_OS_WIN32
/* The strtok() in Microsoft's C library is MT-safe (but still uses
 * only one buffer pointer per thread, but for the use of strtok_r()
 * here that's enough).
 */
#define strtok_r(s,sep,lasts) (*(lasts)=strtok((s),(sep)))
#endif

static void
camel_imap_folder_class_init (CamelImapFolderClass *camel_imap_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_imap_folder_class);

	offline_folder_class = CAMEL_OFFLINE_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_offline_folder_get_type ()));

	/* virtual method overload */
	((CamelObjectClass *)camel_imap_folder_class)->getv = imap_getv;
	((CamelObjectClass *)camel_imap_folder_class)->setv = imap_setv;

	camel_folder_class->get_message = imap_get_message;
	camel_folder_class->rename = imap_rename;
	camel_folder_class->search_by_expression = imap_search_by_expression;
	camel_folder_class->count_by_expression = imap_count_by_expression;
	camel_folder_class->search_by_uids = imap_search_by_uids;
	camel_folder_class->search_free = imap_search_free;
	camel_folder_class->thaw = imap_thaw;
	camel_folder_class->get_quota_info = imap_get_quota_info;
	camel_folder_class->refresh_info = imap_refresh_info;
	camel_folder_class->expunge = imap_expunge;
	camel_folder_class->sync= imap_sync;
	camel_folder_class->append_message = imap_append_online;
	camel_folder_class->sync_message = imap_sync_message;
	camel_folder_class->transfer_messages_to = imap_transfer_online;
	camel_folder_class->get_uncached_uids = imap_get_uncached_uids;
	camel_folder_class->get_filename = imap_get_filename;
}

static void
camel_imap_folder_init (gpointer object, gpointer klass)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (object);
	CamelFolder *folder = CAMEL_FOLDER (object);

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN;

	folder->folder_flags |= (CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY |
				 CAMEL_FOLDER_HAS_SEARCH_CAPABILITY);

	imap_folder->priv = g_malloc0(sizeof(*imap_folder->priv));
#ifdef ENABLE_THREADS
	g_static_mutex_init(&imap_folder->priv->search_lock);
	g_static_rec_mutex_init(&imap_folder->priv->cache_lock);
#endif
	imap_folder->priv->ignore_recent = NULL;

	imap_folder->journal = NULL;
	imap_folder->need_rescan = TRUE;
}

CamelType
camel_imap_folder_get_type (void)
{
	static CamelType camel_imap_folder_type = CAMEL_INVALID_TYPE;

	if (camel_imap_folder_type == CAMEL_INVALID_TYPE) {
		gint i;

		parent_class = camel_offline_folder_get_type();
		camel_imap_folder_type =
			camel_type_register (parent_class, "CamelImapFolder",
					     sizeof (CamelImapFolder),
					     sizeof (CamelImapFolderClass),
					     (CamelObjectClassInitFunc) camel_imap_folder_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_imap_folder_init,
					     (CamelObjectFinalizeFunc) imap_finalize);

		/* only localize here, do not create GSList, we do not want to leak */
		for (i = 0; i < G_N_ELEMENTS (imap_property_list); i++)
			imap_property_list [i].description = _(imap_property_list [i].description);
	}

	return camel_imap_folder_type;
}

static void
replay_offline_journal (CamelImapStore *imap_store, CamelImapFolder *imap_folder, CamelException *ex)
{
	CamelIMAPJournal *imap_journal;

	g_return_if_fail (imap_store != NULL);
	g_return_if_fail (imap_folder != NULL);
	g_return_if_fail (imap_folder->journal != NULL);

	imap_journal = CAMEL_IMAP_JOURNAL (imap_folder->journal);
	g_return_if_fail (imap_journal != NULL);

	/* do not replay when still in offline */
	if (CAMEL_OFFLINE_STORE (imap_store)->state != CAMEL_OFFLINE_STORE_NETWORK_AVAIL || !camel_imap_store_connected (imap_store, ex))
		return;

	/* Check if the replay is already in progress as imap_sync would be called while expunge resync */
	if (!imap_journal->rp_in_progress) {
		imap_journal->rp_in_progress++;

		camel_offline_journal_replay (imap_folder->journal, ex);
		camel_imap_journal_close_folders (imap_journal);
		camel_offline_journal_write (imap_folder->journal, ex);

		imap_journal->rp_in_progress--;
		g_return_if_fail (imap_journal->rp_in_progress >= 0);
	}
}

CamelFolder *
camel_imap_folder_new (CamelStore *parent, const gchar *folder_name,
		       const gchar *folder_dir, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (parent);
	CamelFolder *folder;
	CamelImapFolder *imap_folder;
	const gchar *short_name;
	gchar *summary_file, *state_file, *path;

	if (g_mkdir_with_parents (folder_dir, S_IRWXU) != 0) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not create directory %s: %s"),
				      folder_dir, g_strerror (errno));
		return NULL;
	}

	folder = CAMEL_FOLDER (camel_object_new (camel_imap_folder_get_type ()));
	short_name = strrchr (folder_name, '/');
	if (short_name)
		short_name++;
	else
		short_name = folder_name;
	camel_folder_construct (folder, parent, folder_name, short_name);

	summary_file = g_strdup_printf ("%s/summary", folder_dir);
	folder->summary = camel_imap_summary_new (folder, summary_file);
	g_free (summary_file);
	if (!folder->summary) {
		camel_object_unref (CAMEL_OBJECT (folder));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not load summary for %s"),
				      folder_name);
		return NULL;
	}

	imap_folder = CAMEL_IMAP_FOLDER (folder);
	path = g_strdup_printf ("%s/journal", folder_dir);
	imap_folder->journal = camel_imap_journal_new (imap_folder, path);
	g_free (path);

	/* set/load persistent state */
	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state_file, NULL);
	g_free(state_file);
	camel_object_state_read(folder);

	imap_folder->cache = camel_imap_message_cache_new (folder_dir, folder->summary, ex);
	if (!imap_folder->cache) {
		camel_object_unref (CAMEL_OBJECT (folder));
		return NULL;
	}

	if (!g_ascii_strcasecmp (folder_name, "INBOX")) {
		if ((imap_store->parameters & IMAP_PARAM_FILTER_INBOX))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
		if ((imap_store->parameters & IMAP_PARAM_FILTER_JUNK))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
	} else {
		if ((imap_store->parameters & (IMAP_PARAM_FILTER_JUNK|IMAP_PARAM_FILTER_JUNK_INBOX)) == (IMAP_PARAM_FILTER_JUNK))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
	}

	imap_folder->search = camel_imap_search_new(folder_dir);

	/* do not do that here, as other folders for 'transfer' might not be opened yet
	replay_offline_journal (imap_store, imap_folder, ex);*/

	return folder;
}

/* Called with the store's connect_lock locked */
void
camel_imap_folder_selected (CamelFolder *folder, CamelImapResponse *response,
			    CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapSummary *imap_summary = CAMEL_IMAP_SUMMARY (folder->summary);
	gulong exists = 0, validity = 0, val, uid;
	guint32 perm_flags = 0;
	GData *fetch_data;
	gint i, count;
	gchar *resp, *old_uid;

	count = camel_folder_summary_count (folder->summary);

	for (i = 0; i < response->untagged->len; i++) {
		resp = (gchar *) response->untagged->pdata[i] + 2;

		if (!g_ascii_strncasecmp (resp, "FLAGS ", 6) && !perm_flags) {
			resp += 6;
			imap_parse_flag_list (&resp, &folder->permanent_flags, NULL);
		} else if (!g_ascii_strncasecmp (resp, "OK [PERMANENTFLAGS ", 19)) {
			resp += 19;

			/* workaround for broken IMAP servers that send
			 * "* OK [PERMANENTFLAGS ()] Permanent flags"
			 * even tho they do allow storing flags. */
			imap_parse_flag_list (&resp, &perm_flags, NULL);
			if (perm_flags != 0)
				folder->permanent_flags = perm_flags;
		} else if (!g_ascii_strncasecmp (resp, "OK [UIDVALIDITY ", 16)) {
			validity = strtoul (resp + 16, NULL, 10);
		} else if (isdigit ((guchar)*resp)) {
			gulong num = strtoul (resp, &resp, 10);

			if (!g_ascii_strncasecmp (resp, " EXISTS", 7)) {
				exists = num;
				/* Remove from the response so nothing
				 * else tries to interpret it.
				 */
				g_free (response->untagged->pdata[i]);
				g_ptr_array_remove_index (response->untagged, i--);
			}
		}
	}

	if (camel_strstrcase (response->status, "OK [READ-ONLY]"))
		imap_folder->read_only = TRUE;

/*FIXME what to do here.
	  if (camel_disco_store_status (CAMEL_DISCO_STORE (folder->parent_store)) == CAMEL_DISCO_STORE_RESYNCING) {
		if (validity != imap_summary->validity) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_SUMMARY_INVALID,
					      _("Folder was destroyed and recreated on server."));
			return;
		}

		 FIXME: find missing UIDs ?
		return;
	} */

	if (!imap_summary->validity)
		imap_summary->validity = validity;
	else if (validity != imap_summary->validity) {
		imap_summary->validity = validity;
		camel_folder_summary_clear (folder->summary);
		CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);
		camel_imap_message_cache_clear (imap_folder->cache);
		CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);
		imap_folder->need_rescan = FALSE;
		camel_imap_folder_changed (folder, exists, NULL, ex);
		return;
	}

	/* If we've lost messages, we have to rescan everything */
	if (exists < count)
		imap_folder->need_rescan = TRUE;
	else if (count != 0 && !imap_folder->need_rescan) {
		CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);

		/* Similarly, if the UID of the highest message we
		 * know about has changed, then that indicates that
		 * messages have been both added and removed, so we
		 * have to rescan to find the removed ones. (We pass
		 * NULL for the folder since we know that this folder
		 * is selected, and we don't want camel_imap_command
		 * to worry about it.)
		 */
		response = camel_imap_command (store, NULL, ex, "FETCH %d UID", count);
		if (!response)
			return;
		uid = 0;
		for (i = 0; i < response->untagged->len; i++) {
			resp = response->untagged->pdata[i];
			val = strtoul (resp + 2, &resp, 10);
			if (val == 0)
				continue;
			if (!g_ascii_strcasecmp (resp, " EXISTS")) {
				/* Another one?? */
				exists = val;
				continue;
			}
			if (uid != 0 || val != count || g_ascii_strncasecmp (resp, " FETCH (", 8) != 0)
				continue;

			fetch_data = parse_fetch_response (imap_folder, resp + 7);
			uid = strtoul (g_datalist_get_data (&fetch_data, "UID"), NULL, 10);
			g_datalist_clear (&fetch_data);
		}
		camel_imap_response_free_without_processing (store, response);

		old_uid = camel_folder_summary_uid_from_index (folder->summary, count - 1);
		if (old_uid) {
			val = strtoul (old_uid, NULL, 10);
			g_free (old_uid);
			if (uid == 0 || uid != val)
				imap_folder->need_rescan = TRUE;
		}
	}

	/* Now rescan if we need to */
	if (imap_folder->need_rescan) {
		imap_rescan (folder, exists, ex);
		return;
	}

	/* If we don't need to rescan completely, but new messages
	 * have been added, find out about them.
	 */
	if (exists > count)
		camel_imap_folder_changed (folder, exists, NULL, ex);

	/* And we're done. */
}

static void
imap_finalize (CamelObject *object)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (object);

	if (imap_folder->search)
		camel_object_unref (CAMEL_OBJECT (imap_folder->search));
	if (imap_folder->cache)
		camel_object_unref (CAMEL_OBJECT (imap_folder->cache));

#ifdef ENABLE_THREADS
	g_static_mutex_free(&imap_folder->priv->search_lock);
	g_static_rec_mutex_free(&imap_folder->priv->cache_lock);
#endif
	if (imap_folder->priv->ignore_recent)
		g_hash_table_unref (imap_folder->priv->ignore_recent);

	if (imap_folder->journal) {
		camel_offline_journal_write (imap_folder->journal, NULL);
		camel_object_unref (imap_folder->journal);
	}

	g_free(imap_folder->priv);
}

static gchar *
imap_get_filename (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelImapFolder *imap_folder = (CamelImapFolder*) folder;

	return camel_imap_message_cache_get_filename (imap_folder->cache, uid, "", ex);
}

static gint
imap_getv(CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	gint i, count=0;
	guint32 tag;

	for (i=0;i<args->argc;i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_OBJECT_ARG_PERSISTENT_PROPERTIES:
		case CAMEL_FOLDER_ARG_PROPERTIES: {
			CamelArgGetV props;
			gint i;

			props.argc = 1;
			props.argv[0] = *arg;
			((CamelObjectClass *)parent_class)->getv(object, ex, &props);

			for (i = 0; i < G_N_ELEMENTS (imap_property_list); i++)
				*arg->ca_ptr = g_slist_append (*arg->ca_ptr, &imap_property_list[i]);
			break; }
			/* imap args */
		case CAMEL_IMAP_FOLDER_ARG_CHECK_FOLDER:
			/* The internal value has precedence before the one stored in the summary. */
			*arg->ca_int = ((CamelImapFolder *)object)->check_folder;
			break;
			/* CamelObject args */
		case CAMEL_OBJECT_ARG_DESCRIPTION:
			if (folder->description == NULL) {
				CamelURL *uri = ((CamelService *)folder->parent_store)->url;

				/* what if the full name doesn't incclude /'s?  does it matter? */
				folder->description = g_strdup_printf("%s@%s:%s", uri->user, uri->host, folder->full_name);
			}
			*arg->ca_str = folder->description;
			break;
		default:
			count++;
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	if (count)
		return ((CamelObjectClass *)parent_class)->getv(object, ex, args);

	return 0;
}

static gint
imap_setv (CamelObject *object, CamelException *ex, CamelArgV *args)
{
	gint save = 0;
	gint i;
	guint32 tag;

	for (i = 0; i < args->argc; i++) {
		CamelArg *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_IMAP_FOLDER_ARG_CHECK_FOLDER:
			if (((CamelImapFolder *)object)->check_folder != arg->ca_int) {
				CamelFolder *folder = (CamelFolder *)object;

				((CamelImapFolder *)object)->check_folder = arg->ca_int;
				save = 1;

				/* store both to the summary and to folder cmeta, to have this value restored correctly next time folder is fully loaded */
				if (folder->parent_store && CAMEL_IS_IMAP_STORE (folder->parent_store)) {
					CamelStoreInfo *si;
					CamelStoreSummary *sm = CAMEL_STORE_SUMMARY (((CamelImapStore *)(folder->parent_store))->summary);

					si = camel_store_summary_path (sm, folder->full_name);
					if (si) {
						if ((si->flags & CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW) != 0 ? 1 : 0 != (arg->ca_int) ? 1 : 0) {
							si->flags = (si->flags & (~CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW)) | ((arg->ca_int) ? CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW : 0);
							camel_store_summary_touch (sm);
							camel_store_summary_save (sm);
						}

						camel_store_summary_info_free (sm, si);
					}
				}
			}
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	if (save)
		camel_object_state_write (object);

	return ((CamelObjectClass *)parent_class)->setv (object, ex, args);
}

static void
imap_rename (CamelFolder *folder, const gchar *new)
{
	CamelImapFolder *imap_folder = (CamelImapFolder *)folder;
	CamelImapStore *imap_store = (CamelImapStore *)folder->parent_store;
	gchar *folder_dir, *summary_path, *state_file;
	gchar *folders;

	folders = g_strconcat (imap_store->storage_path, "/folders", NULL);
	folder_dir = imap_path_to_physical (folders, new);
	g_free (folders);
	summary_path = g_strdup_printf("%s/summary", folder_dir);

	CAMEL_IMAP_FOLDER_REC_LOCK (folder, cache_lock);
	camel_imap_message_cache_set_path(imap_folder->cache, folder_dir);
	CAMEL_IMAP_FOLDER_REC_UNLOCK (folder, cache_lock);

	camel_folder_summary_set_filename(folder->summary, summary_path);

	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	camel_object_set(folder, NULL, CAMEL_OBJECT_STATE_FILE, state_file, NULL);
	g_free(state_file);

	g_free(summary_path);
	g_free(folder_dir);

	((CamelFolderClass *)offline_folder_class)->rename(folder, new);
}

/* called with connect_lock locked */
static gboolean
get_folder_status (CamelFolder *folder, guint32 *total, guint32 *unread, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	gboolean res = FALSE;

	g_return_val_if_fail (folder != NULL, FALSE);

	response = camel_imap_command (imap_store, folder, ex, "STATUS %F (MESSAGES UNSEEN)", folder->full_name);

	if (response) {
		gint i;

		for (i = 0; i < response->untagged->len; i++) {
			const gchar *resp = response->untagged->pdata[i];

			if (resp && g_str_has_prefix (resp, "* STATUS ")) {
				const gchar *p = NULL;

				while (*resp) {
					if (*resp == '(')
						p = resp;
					resp++;
				}

				if (p && *(resp - 1) == ')') {
					const gchar *msgs = NULL, *unseen = NULL;

					p++;

					while (p && (!msgs || !unseen)) {
						const gchar **dest = NULL;

						if (g_str_has_prefix (p, "MESSAGES "))
							dest = &msgs;
						else if (g_str_has_prefix (p, "UNSEEN "))
							dest = &unseen;

						if (dest) {
							*dest = imap_next_word (p);

							if (!*dest)
								break;

							p = imap_next_word (*dest);
						} else {
							p = imap_next_word (p);
							if (p)
								p = imap_next_word (p);
						}
					}

					if (msgs && unseen) {
						res = TRUE;

						if (total)
							*total = strtoul (msgs, NULL, 10);

						if (unread)
							*unread = strtoul (unseen, NULL, 10);
					}
				}
			}
		}
		camel_imap_response_free (imap_store, response);
	}

	return res;
}

static void
imap_refresh_info (CamelFolder *folder, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapResponse *response;
	CamelStoreInfo *si;
	gint check_rescan = -1;

	if (CAMEL_OFFLINE_STORE (imap_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return;

	if (camel_folder_is_frozen (folder)) {
		imap_folder->need_refresh = TRUE;
		return;
	}

	/* If the folder isn't selected, select it (which will force
	 * a rescan if one is needed).
	 * Also, if this is the INBOX, some servers (cryus) wont tell
	 * us with a NOOP of new messages, so force a reselect which
	 * should do it.  */
	CAMEL_SERVICE_REC_LOCK (imap_store, connect_lock);

	if (camel_application_is_exiting  || !camel_imap_store_connected(imap_store, ex))
		goto done;

	/* try to store local changes first, as the summary contains new local messages */
	replay_offline_journal (imap_store, imap_folder, ex);

	if (imap_store->current_folder != folder
	    || g_ascii_strcasecmp(folder->full_name, "INBOX") == 0) {
		response = camel_imap_command (imap_store, folder, ex, NULL);
		if (response) {
			camel_imap_folder_selected (folder, response, ex);
			camel_imap_response_free (imap_store, response);
		}
	} else if (imap_folder->need_rescan) {
		/* Otherwise, if we need a rescan, do it, and if not, just do
		 * a NOOP to give the server a chance to tell us about new
		 * messages.
		 */
		imap_rescan (folder, camel_folder_summary_count (folder->summary), ex);
		check_rescan = 0;
	} else {
#if 0
		/* on some servers need to CHECKpoint INBOX to recieve new messages?? */
		/* rfc2060 suggests this, but havent seen a server that requires it */
		if (g_ascii_strcasecmp(folder->full_name, "INBOX") == 0) {
			response = camel_imap_command (imap_store, folder, ex, "CHECK");
			camel_imap_response_free (imap_store, response);
		}
#endif
		response = camel_imap_command (imap_store, folder, ex, "NOOP");
		camel_imap_response_free (imap_store, response);
	}

	si = camel_store_summary_path((CamelStoreSummary *)((CamelImapStore *)folder->parent_store)->summary, folder->full_name);
	if (si) {
		guint32 unread, total;

		camel_object_get(folder, NULL, CAMEL_FOLDER_TOTAL, &total, CAMEL_FOLDER_UNREAD, &unread, NULL);
		if (si->total != total
		    || si->unread != unread) {
			si->total = total;
			si->unread = unread;
			camel_store_summary_touch((CamelStoreSummary *)((CamelImapStore *)folder->parent_store)->summary);
			check_rescan = 0;
		}
		camel_store_summary_info_free((CamelStoreSummary *)((CamelImapStore *)folder->parent_store)->summary, si);
	}

	if (check_rescan && !camel_application_is_exiting && !camel_exception_is_set (ex)) {
		if (check_rescan == -1) {
			guint32 total, unread = 0, server_total = 0, server_unread = 0;

			check_rescan = 0;

			/* Check whether there are changes in total/unread messages in the folders
			   and if so, then rescan whole summary */
			if (get_folder_status (folder, &server_total, &server_unread, ex)) {
				camel_object_get (folder, NULL, CAMEL_FOLDER_TOTAL, &total, CAMEL_FOLDER_UNREAD, &unread, NULL);

				if (total != server_total || unread != server_unread)
					check_rescan = 1;
			}
		}

		if (check_rescan)
			imap_rescan (folder, camel_folder_summary_count (folder->summary), ex);
	}
done:
	CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);

	camel_folder_summary_save_to_db (folder->summary, ex);
	camel_store_summary_save ((CamelStoreSummary *)((CamelImapStore *)folder->parent_store)->summary);
}

static void
fillup_custom_flags (CamelMessageInfo *mi, gchar *custom_flags)
{
	gchar **array_str;
	gint index = 0;

	array_str = g_strsplit (custom_flags, " ", -1);

	while (array_str[index] != NULL) {
		camel_flag_set(&((CamelMessageInfoBase *)mi)->user_flags, array_str[index], TRUE);
		++ index;
	}

	g_strfreev (array_str);
}

/* This will merge custom flags with those in message info. Returns whether was some change. */
static gboolean
merge_custom_flags (CamelMessageInfo *mi, const gchar *custom_flags)
{
	GSList *list, *p;
	GHashTable *server;
	gchar **cflags;
	gint i;
	const CamelFlag *flag;
	gboolean changed = FALSE;

	g_return_val_if_fail (mi != NULL, FALSE);

	if (!custom_flags)
		custom_flags = "";

	list = NULL;
	server = g_hash_table_new (g_str_hash, g_str_equal);

	cflags = g_strsplit (custom_flags, " ", -1);
	for (i = 0; cflags [i]; i++) {
		gchar *name = cflags [i];

		if (name && *name) {
			g_hash_table_insert (server, name, name);
			list = g_slist_prepend (list, name);
		}
	}

	for (flag = camel_message_info_user_flags (mi); flag; flag = flag->next) {
		gchar *name = (gchar *)flag->name;

		if (name && *name)
			list = g_slist_prepend (list, name);
	}

	list = g_slist_sort (list, (GCompareFunc) strcmp);
	for (p = list; p; p = p->next) {
		if (p->next && strcmp (p->data, p->next->data) == 0) {
			/* This flag is there twice, which means it was on the server and
			   in our local summary too; thus skip these two elements. */
			p = p->next;
		} else {
			/* If this value came from the server, then add it to our local summary,
			   otherwise it was in local summary, but isn't on the server, thus remove it. */
			changed = TRUE;
			mi->dirty = TRUE;
			if (mi->summary)
				camel_folder_summary_touch (mi->summary);
			camel_flag_set (&((CamelMessageInfoBase *)mi)->user_flags, p->data, g_hash_table_lookup (server, p->data) != NULL);
			((CamelMessageInfoBase *) mi)->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		}
	}

	g_slist_free (list);
	g_hash_table_destroy (server);
	g_strfreev (cflags);

	return changed;
}

/* Called with the store's connect_lock locked */
static void
imap_rescan (CamelFolder *folder, gint exists, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	struct {
		gchar *uid;
		guint32 flags;
		gchar *custom_flags;
	} *new;
	gchar *resp, *uid;
	CamelImapResponseType type;
	gint i, j, seq, summary_got, del=0;
	guint summary_len;
	CamelMessageInfo *info;
	CamelImapMessageInfo *iinfo;
	GArray *removed;
	gboolean ok;
	CamelFolderChangeInfo *changes = NULL;

	if (camel_application_is_exiting)
		return;

	imap_folder->need_rescan = FALSE;

	summary_len = camel_folder_summary_count (folder->summary);
	if (summary_len == 0) {
		if (exists)
			camel_imap_folder_changed (folder, exists, NULL, ex);
		return;
	}

	/* Check UIDs and flags of all messages we already know of. */
	camel_operation_start (NULL, _("Scanning for changed messages in %s"), folder->name);
	uid = camel_folder_summary_uid_from_index (folder->summary, summary_len - 1);

	if (!uid) {
		camel_operation_end (NULL);
		return;
	}

	ok = camel_imap_command_start (store, folder, ex,
				       "UID FETCH 1:%s (FLAGS)",
				       uid);
	g_free (uid);
	if (!ok) {
		camel_operation_end (NULL);
		return;
	}

	new = g_malloc0 (summary_len * sizeof (*new));
	summary_got = 0;
	while ((type = camel_imap_command_response (store, &resp, ex)) == CAMEL_IMAP_RESPONSE_UNTAGGED && !camel_application_is_exiting) {
		GData *data;
		gchar *uid;
		guint32 flags;

		data = parse_fetch_response (imap_folder, resp);
		g_free (resp);
		if (!data)
			continue;

		seq = GPOINTER_TO_INT (g_datalist_get_data (&data, "SEQUENCE"));
		uid = g_datalist_get_data (&data, "UID");
		flags = GPOINTER_TO_UINT (g_datalist_get_data (&data, "FLAGS"));

		if (!uid || !seq || seq > summary_len || seq < 0) {
			g_datalist_clear (&data);
			continue;
		}

		camel_operation_progress (NULL, ++summary_got * 100 / summary_len);
		new[seq - 1].uid = g_strdup (uid);
		new[seq - 1].flags = flags;
		new[seq - 1].custom_flags = g_strdup (g_datalist_get_data (&data, "CUSTOM.FLAGS"));
		g_datalist_clear (&data);
	}

	if (summary_got == 0 && summary_len == 0) {
		camel_operation_end (NULL);
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		g_free(new);
		return;
	}
	camel_operation_end (NULL);
	if (type == CAMEL_IMAP_RESPONSE_ERROR || camel_application_is_exiting) {
		for (i = 0; i < summary_len && new[i].uid; i++) {
			g_free (new[i].uid);
			g_free (new[i].custom_flags);
		}
		g_free (new);

		if (type != CAMEL_IMAP_RESPONSE_ERROR && type != CAMEL_IMAP_RESPONSE_TAGGED)
			CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

		return;
	}

	/* Free the final tagged response */
	g_free (resp);

	/* If we find a UID in the summary that doesn't correspond to
	 * the UID in the folder, then either: (a) it's a real UID,
	 * but the message was deleted on the server, or (b) it's a
	 * fake UID, and needs to be removed from the summary in order
	 * to sync up with the server. So either way, we remove it
	 * from the summary.
	 */
	removed = g_array_new (FALSE, FALSE, sizeof (gint));

	if (summary_len - camel_folder_summary_cache_size (folder->summary) > 50)
		camel_folder_summary_reload_from_db (folder->summary, ex);

	for (i = 0, j = 0; i < summary_len && new[j].uid; i++) {
		gboolean changed = FALSE;

		uid = camel_folder_summary_uid_from_index (folder->summary, i);

		if (!uid)
			continue;

		info = camel_folder_summary_uid (folder->summary, uid);
		if (!info) {
			if (g_getenv("CRASH_IMAP")) { /* Debug logs to tackle on hard to get imap crasher */
				printf("CRASH: %s: %s", folder->full_name, uid);
				g_assert(0);
			} else
				continue;
		}

		iinfo = (CamelImapMessageInfo *)info;

		if (strcmp (uid, new[j].uid) != 0) {
			g_free (uid);

			/* these will be deleted from db in a moment. So adjust the counts please */
			if (info) {
				CamelMessageInfoBase *dinfo = (CamelMessageInfoBase *) info;
				gint unread=0, deleted=0, junk=0;
				guint32 flags;

				flags = dinfo->flags;
				if (!(flags & CAMEL_MESSAGE_SEEN))
					unread = 1;

				if (flags & CAMEL_MESSAGE_DELETED)
					deleted = 1;

				if (flags & CAMEL_MESSAGE_JUNK)
					junk = 1;

				if (unread)
					folder->summary->unread_count--;

				if (deleted)
					folder->summary->deleted_count--;
				if (junk)
					folder->summary->junk_count--;

				if (junk && !deleted)
					folder->summary->junk_not_deleted_count--;

				if (!junk &&  !deleted)
					folder->summary->visible_count--;

				folder->summary->saved_count--;
			}
			seq = i + 1-del;
			del++;
			g_array_append_val (removed, seq);
			camel_message_info_free(info);
			continue;
		}

		g_free (uid);

		/* Update summary flags */

		if (new[j].flags != iinfo->server_flags) {
			guint32 server_set, server_cleared;
			gint read=0, deleted=0, junk=0;

			server_set = new[j].flags & ~iinfo->server_flags;
			server_cleared = iinfo->server_flags & ~new[j].flags;

			if (server_set & CAMEL_MESSAGE_SEEN)
				read = 1;
			else if (server_cleared & CAMEL_MESSAGE_SEEN)
				read = -1;

			if (server_set & CAMEL_MESSAGE_DELETED)
				deleted = 1;
			else if (server_cleared & CAMEL_MESSAGE_DELETED)
				deleted = -1;

			if (server_set & CAMEL_MESSAGE_JUNK)
				junk = 1;
			else if (server_cleared & CAMEL_MESSAGE_JUNK)
				junk = -1;

			d(printf("%s %s %s %s\n", iinfo->info.uid, read == 1 ? "read" : ( read == -1 ? "unread" : ""),
				 deleted == 1 ? "deleted" : ( deleted == -1 ? "undeleted" : ""),
				 junk == 1 ? "junk" : ( junk == -1 ? "unjunked" : "")));

			if (read)
				folder->summary->unread_count -= read;
			if (deleted)
				folder->summary->deleted_count += deleted;
			if (junk)
				folder->summary->junk_count += junk;
			if (junk && !deleted)
				folder->summary->junk_not_deleted_count += junk;
			if (junk ||  deleted)
				folder->summary->visible_count -= junk ? junk : deleted;

			iinfo->info.flags = (iinfo->info.flags | server_set) & ~server_cleared;
			iinfo->server_flags = new[j].flags;
			iinfo->info.dirty = TRUE;
			if (info->summary)
				camel_folder_summary_touch (info->summary);
			changed = TRUE;
		}

		/* Do not merge custom flags when server doesn't support it.
		   Because server always reports NULL, which means none, which
		   will remove user's flags from local machine, which is bad.
		*/
		if ((folder->permanent_flags & CAMEL_MESSAGE_USER) != 0 && merge_custom_flags (info, new[j].custom_flags))
			changed = TRUE;

		if (changed) {
			if (changes == NULL)
				changes = camel_folder_change_info_new();
			camel_folder_change_info_change_uid(changes, new[j].uid);
		}

		camel_message_info_free(info);
		g_free (new[j].uid);
		g_free (new[j].custom_flags);
		j++;
	}

	if (changes) {
		camel_object_trigger_event(CAMEL_OBJECT (folder), "folder_changed", changes);
		camel_folder_change_info_free(changes);
	}

	seq = i + 1;

#if 0
	/* FIXME: Srini: I don't think this will be called any longer. */
	/* Free remaining memory. */
	while (i < summary_len && new[i].uid) {
		g_free (new[i].uid);
		g_free (new[i].custom_flags);
		i++;
	}
#endif
	g_free (new);

	/* Remove any leftover cached summary messages. (Yes, we
	 * repeatedly add the same number to the removed array.
	 * See RFC2060 7.4.1)
	 */

	for (i = seq; i <= summary_len; i++) {
		CamelMessageInfoBase *dinfo;
		gint j;
		dinfo = (CamelMessageInfoBase *) camel_folder_summary_index (folder->summary, i-1);
		if (dinfo) {
			/* these will be deleted from db in a moment. So adjust the counts please */
			gint unread=0, deleted=0, junk=0;
			guint32 flags;

			flags = dinfo->flags;
			if (!(flags & CAMEL_MESSAGE_SEEN))
				unread = 1;

			if (flags & CAMEL_MESSAGE_DELETED)
				deleted = 1;

			if (flags & CAMEL_MESSAGE_JUNK)
				junk = 1;

			if (unread)
				folder->summary->unread_count--;

			if (deleted)
				folder->summary->deleted_count--;
			if (junk)
				folder->summary->junk_count--;

			if (junk && !deleted)
				folder->summary->junk_not_deleted_count--;

			if (!junk &&  !deleted)
				folder->summary->visible_count--;

			folder->summary->saved_count--;
			camel_message_info_free(dinfo);
		}
		j = seq - del;
		g_array_append_val (removed, j);
	}
	/* And finally update the summary. */
	camel_imap_folder_changed (folder, exists, removed, ex);
	g_array_free (removed, TRUE);
}

static const gchar *
get_message_uid (CamelFolder *folder, CamelImapMessageInfo *info)
{
	const gchar *uid;

	g_return_val_if_fail (folder != NULL, NULL);
	g_return_val_if_fail (info != NULL, NULL);

	uid = camel_message_info_uid (info);
	g_return_val_if_fail (uid != NULL, NULL);

	if (!isdigit ((guchar)*uid)) {
		uid = camel_imap_journal_uidmap_lookup ((CamelIMAPJournal *) CAMEL_IMAP_FOLDER (folder)->journal, uid);
		g_return_val_if_fail (uid != NULL, NULL);
	}

	return uid;
}

/* the max number of chars that an unsigned 32-bit gint can be is 10 chars plus 1 for a possible : */
#define UID_SET_FULL(setlen, maxlen) (maxlen > 0 ? setlen + 11 >= maxlen : FALSE)

/* Find all messages in @folder with flags matching @flags and @mask.
 * If no messages match, returns %NULL. Otherwise, returns an array of
 * CamelMessageInfo and sets *@set to a message set corresponding the
 * UIDs of the matched messages (up to @UID_SET_LIMIT bytes). The
 * caller must free the infos, the array, and the set string.
 */
static GPtrArray *
get_matching (CamelFolder *folder, guint32 flags, guint32 mask, CamelMessageInfo *master_info, gchar **set, GPtrArray *summary)
{
	GPtrArray *matches;
	CamelImapMessageInfo *info;
	gint i, max, range, last_range_uid;
	GString *gset;
	GSList *list1 = NULL;
	gint count1 = 0;
	gchar *uid;

	/* use the local rinfo in the close_range, because we want to keep our info untouched */
	#define close_range()										\
		if (range != -1) {									\
			if (range != i - 1) {								\
				CamelImapMessageInfo *rinfo = matches->pdata[matches->len - 1];		\
													\
				g_string_append_printf (gset, ":%s", get_message_uid (folder, rinfo));	\
			}										\
			range = -1;									\
			last_range_uid = -1;								\
		}

	matches = g_ptr_array_new ();
	gset = g_string_new ("");
	max = summary->len;
	range = -1;
	last_range_uid = -1;
	for (i = 0; i < max && !UID_SET_FULL (gset->len, UID_SET_LIMIT); i++) {
		gint uid_num;
		uid = summary->pdata[i];

		if (uid) {
			info = (CamelImapMessageInfo *) camel_folder_summary_uid (folder->summary, uid);
		} else
			continue;

		if (!info)
			continue;

		/* if the resulting flag list is empty, then "concat" other message
		   only when server_flags are same, because there will be a flag removal
		   command for this type of situation */
		if ((info->info.flags & mask) != flags || (flags == 0 && info->server_flags != ((CamelImapMessageInfo *)master_info)->server_flags)) {
			camel_message_info_free((CamelMessageInfo *)info);
			close_range ();
			continue;
		}

		uid_num = atoi (uid);

		/* we got only changes, thus the uid's can be mixed up, not the consecutive list,
		   thus close range if we are not in it */
		if (last_range_uid != -1 && uid_num != last_range_uid + 1) {
			close_range ();
		}

		/* only check user flags when we see other message than our 'master' */
		if (strcmp (master_info->uid, ((CamelMessageInfo *)info)->uid)) {
			const CamelFlag *flag;
			GSList *list2 = NULL, *l1, *l2;
			gint count2 = 0, cmp = 0;

			if (!list1) {
				for (flag = camel_message_info_user_flags (master_info); flag; flag = flag->next) {
					if (flag->name && *flag->name) {
						count1++;
						list1 = g_slist_prepend (list1, (gchar *)flag->name);
					}
				}

				list1 = g_slist_sort (list1, (GCompareFunc) strcmp);
			}

			for (flag = camel_message_info_user_flags (info); flag; flag = flag->next) {
				if (flag->name && *flag->name) {
					count2++;
					list2 = g_slist_prepend (list2, (gchar *)flag->name);
				}
			}

			if (count1 != count2) {
				g_slist_free (list2);
				close_range ();
				continue;
			}

			list2 = g_slist_sort (list2, (GCompareFunc) strcmp);
			for (l1 = list1, l2 = list2; l1 && l2 && !cmp; l1 = l1->next, l2 = l2->next) {
				cmp = strcmp (l1->data, l2->data);
			}

			if (cmp) {
				g_slist_free (list2);
				close_range ();
				continue;
			}
		}

		g_ptr_array_add (matches, info);
		/* Remove the uid from the list, to optimize*/
		camel_pstring_free(summary->pdata[i]);
		summary->pdata[i] = NULL;

		if (range != -1) {
			last_range_uid = uid_num;
			continue;
		}

		range = i;
		last_range_uid = uid_num;
		if (gset->len)
			g_string_append_c (gset, ',');
		g_string_append_printf (gset, "%s", get_message_uid (folder, info));
	}

	if (range != -1 && range != max - 1) {
		info = matches->pdata[matches->len - 1];
		g_string_append_printf (gset, ":%s", get_message_uid (folder, info));
	}

	if (list1)
		g_slist_free (list1);

	if (matches->len) {
		*set = gset->str;
		g_string_free (gset, FALSE);
		return matches;
	} else {
		*set = NULL;
		g_string_free (gset, TRUE);
		g_ptr_array_free (matches, TRUE);
		return NULL;
	}

	#undef close_range
}

static void
imap_sync_offline (CamelFolder *folder, CamelException *ex)
{
	if (folder->summary && (folder->summary->flags & CAMEL_SUMMARY_DIRTY) != 0) {
		CamelStoreInfo *si;

		/* ... and store's summary when folder's summary is dirty */
		si = camel_store_summary_path ((CamelStoreSummary *)((CamelImapStore *)folder->parent_store)->summary, folder->full_name);
		if (si) {
			if (si->total != folder->summary->saved_count || si->unread != folder->summary->unread_count) {
				si->total = folder->summary->saved_count;
				si->unread = folder->summary->unread_count;
				camel_store_summary_touch ((CamelStoreSummary *)((CamelImapStore *)folder->parent_store)->summary);
			}

			camel_store_summary_info_free ((CamelStoreSummary *)((CamelImapStore *)folder->parent_store)->summary, si);
		}
	}

	camel_folder_summary_save_to_db (folder->summary, ex);
	camel_store_summary_save((CamelStoreSummary *)((CamelImapStore *)folder->parent_store)->summary);
}

static void
imap_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapMessageInfo *info;
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelException local_ex;

	GPtrArray *matches, *summary;
	gchar *set, *flaglist, *uid;
	gint i, j, max;

	if (folder->permanent_flags == 0 || CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		if (expunge) {
			imap_expunge (folder, ex);
			if (camel_exception_is_set (ex))
				return;
		}
		imap_sync_offline (folder, ex);
		return;
	}

	camel_exception_init (&local_ex);
	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	/* write local changes first */
	replay_offline_journal (store, imap_folder, ex);

	/* Find a message with changed flags, find all of the other
	 * messages like it, sync them as a group, mark them as
	 * updated, and continue.
	 */
	summary = camel_folder_summary_get_changed (folder->summary); /* These should be in memory anyways */
	camel_folder_sort_uids (folder, summary);
	max = summary->len;

	for (i = 0; i < max; i++) {
		gboolean unset = FALSE;
		CamelImapResponse *response = NULL;

		uid = summary->pdata[i];

		if (!uid) /* Possibly it was sync by matching flags, which we NULLify */
			continue;

		if (!(info = (CamelImapMessageInfo *) camel_folder_summary_uid (folder->summary, uid))) {
			continue;
		}

		if (!(info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			camel_message_info_free((CamelMessageInfo *)info);
			continue;
		}

		/* Note: get_matching() uses UID_SET_LIMIT to limit
		   the size of the uid-set string. We don't have to
		   loop here to flush all the matching uids because
		   they will be scooped up later by our parent loop (I
		   think?). -- Jeff */
		matches = get_matching (folder, info->info.flags & (folder->permanent_flags | CAMEL_MESSAGE_FOLDER_FLAGGED),
					folder->permanent_flags | CAMEL_MESSAGE_FOLDER_FLAGGED, (CamelMessageInfo *)info, &set, summary);
		if (matches == NULL) {
			camel_message_info_free(info);
			continue;
		}

		/* Make sure we're connected before issuing commands */
		if (!camel_imap_store_connected(store, ex)) {
			g_free(set);
			break;
		}

		flaglist = imap_create_flag_list (info->info.flags & folder->permanent_flags, (CamelMessageInfo *)info, folder->permanent_flags);

		if (strcmp (flaglist, "()") == 0) {
			/* Note: Cyrus is broken and will not accept an
			   empty-set of flags so... if this is true then we
			   set and unset \Seen flag. It's necessary because
			   we do not know the previously set user flags. */
			unset = TRUE;
			g_free (flaglist);

			/* unset all known server flags, because there left none in the actual flags */
			flaglist =  imap_create_flag_list (info->server_flags & folder->permanent_flags, (CamelMessageInfo *)info, folder->permanent_flags);

			if (strcmp (flaglist, "()") == 0) {
				/* this should not happen, really */
				g_free (flaglist);
				flaglist = strdup ("(\\Seen)");

				response = camel_imap_command (store, folder, &local_ex,
						"UID STORE %s +FLAGS.SILENT %s",
						set, flaglist);
				if (response)
					camel_imap_response_free (store, response);

				response = NULL;
			}
		}

		/* We don't use the info any more */
		camel_message_info_free (info);

		/* Note: to 'unset' flags, use -FLAGS.SILENT (<flag list>) */
		if (!camel_exception_is_set (&local_ex)) {
			response = camel_imap_command (store, folder, &local_ex,
					       "UID STORE %s %sFLAGS.SILENT %s",
					       set, unset ? "-" : "", flaglist);
		}

		g_free (set);
		g_free (flaglist);

		if (response)
			camel_imap_response_free (store, response);

		if (!camel_exception_is_set (&local_ex)) {
			for (j = 0; j < matches->len; j++) {
				info = matches->pdata[j];
				info->info.flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
				((CamelImapMessageInfo *) info)->server_flags =	info->info.flags & CAMEL_IMAP_SERVER_FLAGS;
				info->info.dirty = TRUE; /* Sync it back to the DB */
				if (((CamelMessageInfo *) info)->summary)
					camel_folder_summary_touch (((CamelMessageInfo *) info)->summary);
			}
			camel_folder_summary_touch (folder->summary);
		}

		for (j = 0; j < matches->len; j++) {
			info = matches->pdata[j];
			camel_message_info_free(&info->info);
		}
		g_ptr_array_free (matches, TRUE);

		/* We unlock here so that other threads can have a chance to grab the connect_lock */
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

		/* check for an exception */
		if (camel_exception_is_set (&local_ex)) {
			camel_exception_xfer (ex, &local_ex);
			return;
		}

		/* Re-lock the connect_lock */
		CAMEL_SERVICE_REC_LOCK (store, connect_lock);
	}

	if (expunge)
		imap_expunge (folder, ex);

	g_ptr_array_foreach (summary, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (summary, TRUE);

	/* Save the summary */
	imap_sync_offline (folder, ex);

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
}

static gint
uid_compar (gconstpointer va, gconstpointer vb)
{
	const gchar **sa = (const gchar **)va, **sb = (const gchar **)vb;
	gulong a, b;

	a = strtoul (*sa, NULL, 10);
	b = strtoul (*sb, NULL, 10);
	if (a < b)
		return -1;
	else if (a == b)
		return 0;
	else
		return 1;
}

static void
imap_expunge_uids_offline (CamelFolder *folder, GPtrArray *uids, CamelException *ex)
{
	CamelFolderChangeInfo *changes;
	GSList *list = NULL;
	gint i;

	qsort (uids->pdata, uids->len, sizeof (gpointer), uid_compar);

	changes = camel_folder_change_info_new ();

	for (i = 0; i < uids->len; i++) {
		camel_folder_summary_remove_uid_fast (folder->summary, uids->pdata[i]);
		camel_folder_change_info_remove_uid (changes, uids->pdata[i]);
		list = g_slist_prepend (list, (gpointer) uids->pdata[i]);
		/* We intentionally don't remove it from the cache because
		 * the cached data may be useful in replaying a COPY later.
		 */
	}
	camel_db_delete_uids (folder->parent_store->cdb_w, folder->full_name, list, ex);
	g_slist_free(list);
	camel_folder_summary_save_to_db (folder->summary, ex);

	camel_imap_journal_log (CAMEL_IMAP_FOLDER (folder)->journal,
			       CAMEL_IMAP_JOURNAL_ENTRY_EXPUNGE, uids);

	camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", changes);
	camel_folder_change_info_free (changes);
}

static void
imap_expunge_uids_online (CamelFolder *folder, GPtrArray *uids, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	gint uid = 0;
	gchar *set;
	gboolean full_expunge = (store->capabilities & IMAP_CAPABILITY_UIDPLUS) == 0;
	CamelFolderChangeInfo *changes;
	gint i;
	GSList *list=NULL;

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	if ((store->capabilities & IMAP_CAPABILITY_UIDPLUS) == 0) {
		((CamelFolderClass *)CAMEL_OBJECT_GET_CLASS(folder))->sync(folder, 0, ex);
		if (camel_exception_is_set(ex)) {
			CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
			return;
		}
	}

	qsort (uids->pdata, uids->len, sizeof (gpointer), uid_compar);

	while (uid < uids->len) {
		set = imap_uid_array_to_set (folder->summary, uids, uid, UID_SET_LIMIT, &uid);
		response = camel_imap_command (store, folder, ex,
					       "UID STORE %s +FLAGS.SILENT (\\Deleted)",
					       set);
		if (response)
			camel_imap_response_free (store, response);
		if (camel_exception_is_set (ex)) {
			CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
			g_free (set);
			return;
		}

		if (!full_expunge) {
			response = camel_imap_command (store, folder, ex,
						       "UID EXPUNGE %s", set);

			if (camel_exception_is_set (ex)) {
				g_debug (G_STRLOC ": 'UID EXPUNGE %s' failed: %s (0x%x)", set, camel_exception_get_description (ex), camel_exception_get_id (ex));
				camel_exception_clear (ex);

				/* UID EXPUNGE failed, something is broken on the server probably,
				   thus fall back to the full expunge. It's not so good, especially
				   when resyncing, it will remove already marked messages on the
				   server too. I guess that's fine anyway, isn't it?
				   For failed command see Gnome's bug #536486 */
				full_expunge = TRUE;
			}
		}

		if (full_expunge)
			response = camel_imap_command (store, folder, ex, "EXPUNGE");

		if (response)
			camel_imap_response_free (store, response);

		g_free (set);
	}

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

	changes = camel_folder_change_info_new ();
	for (i = 0; i < uids->len; i++) {
		camel_folder_summary_remove_uid_fast (folder->summary, uids->pdata[i]);
		camel_folder_change_info_remove_uid (changes, uids->pdata[i]);
		list = g_slist_prepend (list, (gpointer) uids->pdata[i]);
		/* We intentionally don't remove it from the cache because
		 * the cached data may be useful in replaying a COPY later.
		 */
	}
	camel_db_delete_uids (folder->parent_store->cdb_w, folder->full_name, list, ex);
	g_slist_free (list);
	camel_folder_summary_save_to_db (folder->summary, ex);
	camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", changes);
	camel_folder_change_info_free (changes);
}

static void
imap_expunge (CamelFolder *folder, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	GPtrArray *uids;

	camel_folder_summary_save_to_db (folder->summary, ex);
	uids = camel_db_get_folder_deleted_uids (folder->parent_store->cdb_r, folder->full_name, ex);
	if (!uids)
		return;

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL)
		imap_expunge_uids_online (folder, uids, ex);
	else
		imap_expunge_uids_offline (folder, uids, ex);

	g_ptr_array_foreach (uids, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (uids, TRUE);
}

void
imap_expunge_uids_resyncing (CamelFolder *folder, GPtrArray *uids, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	GPtrArray *keep_uids, *mark_uids;
	CamelImapResponse *response;
	gchar *result;

	if (imap_folder->read_only)
		return;

	if (store->capabilities & IMAP_CAPABILITY_UIDPLUS) {
		imap_expunge_uids_online (folder, uids, ex);
		return;
	}

	/* If we don't have UID EXPUNGE we need to avoid expunging any
	 * of the wrong messages. So we search for deleted messages,
	 * and any that aren't in our to-expunge list get temporarily
	 * marked un-deleted.
	 */

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);

	((CamelFolderClass *)CAMEL_OBJECT_GET_CLASS(folder))->sync(folder, 0, ex);
	if (camel_exception_is_set(ex)) {
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return;
	}

	response = camel_imap_command (store, folder, ex, "UID SEARCH DELETED");
	if (!response) {
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return;
	}
	result = camel_imap_response_extract (store, response, "SEARCH", ex);
	if (!result) {
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return;
	}

	if (result[8] == ' ') {
		gchar *uid, *lasts = NULL;
		gulong euid, kuid;
		gint ei, ki;

		keep_uids = g_ptr_array_new ();
		mark_uids = g_ptr_array_new ();

		/* Parse SEARCH response */
		for (uid = strtok_r (result + 9, " ", &lasts); uid; uid = strtok_r (NULL, " ", &lasts))
			g_ptr_array_add (keep_uids, uid);
		qsort (keep_uids->pdata, keep_uids->len,
		       sizeof (gpointer), uid_compar);

		/* Fill in "mark_uids", empty out "keep_uids" as needed */
		for (ei = ki = 0; ei < uids->len; ei++) {
			euid = strtoul (uids->pdata[ei], NULL, 10);

			for (kuid = 0; ki < keep_uids->len; ki++) {
				kuid = strtoul (keep_uids->pdata[ki], NULL, 10);

				if (kuid >= euid)
					break;
			}

			if (euid == kuid)
				g_ptr_array_remove_index (keep_uids, ki);
			else
				g_ptr_array_add (mark_uids, uids->pdata[ei]);
		}
	} else {
		/* Empty SEARCH result, meaning nothing is marked deleted
		 * on server.
		 */

		keep_uids = NULL;
		mark_uids = uids;
	}

	/* Unmark messages to be kept */

	if (keep_uids) {
		gchar *uidset;
		gint uid = 0;

		while (uid < keep_uids->len) {
			uidset = imap_uid_array_to_set (folder->summary, keep_uids, uid, UID_SET_LIMIT, &uid);

			response = camel_imap_command (store, folder, ex,
						       "UID STORE %s -FLAGS.SILENT (\\Deleted)",
						       uidset);

			g_free (uidset);

			if (!response) {
				g_ptr_array_free (keep_uids, TRUE);
				g_ptr_array_free (mark_uids, TRUE);
				CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
				return;
			}
			camel_imap_response_free (store, response);
		}
	}

	/* Mark any messages that still need to be marked */
	if (mark_uids) {
		gchar *uidset;
		gint uid = 0;

		while (uid < mark_uids->len) {
			uidset = imap_uid_array_to_set (folder->summary, mark_uids, uid, UID_SET_LIMIT, &uid);

			response = camel_imap_command (store, folder, ex,
						       "UID STORE %s +FLAGS.SILENT (\\Deleted)",
						       uidset);

			g_free (uidset);

			if (!response) {
				g_ptr_array_free (keep_uids, TRUE);
				g_ptr_array_free (mark_uids, TRUE);
				CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
				return;
			}
			camel_imap_response_free (store, response);
		}

		if (mark_uids != uids)
			g_ptr_array_free (mark_uids, TRUE);
	}

	/* Do the actual expunging */
	response = camel_imap_command (store, folder, ex, "EXPUNGE");
	if (response)
		camel_imap_response_free (store, response);

	/* And fix the remaining messages if we mangled them */
	if (keep_uids) {
		gchar *uidset;
		gint uid = 0;

		while (uid < keep_uids->len) {
			uidset = imap_uid_array_to_set (folder->summary, keep_uids, uid, UID_SET_LIMIT, &uid);

			/* Don't pass ex if it's already been set */
			response = camel_imap_command (store, folder,
						       camel_exception_is_set (ex) ? NULL : ex,
						       "UID STORE %s +FLAGS.SILENT (\\Deleted)",
						       uidset);

			g_free (uidset);
			if (response)
				camel_imap_response_free (store, response);
		}

		g_ptr_array_free (keep_uids, TRUE);
	}

	/* now we can free this, now that we're done with keep_uids */
	g_free (result);

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
}

static gchar *
get_temp_uid (void)
{
	gchar *res;

	static gint counter = 0;
	G_LOCK_DEFINE_STATIC (lock);

	G_LOCK (lock);
	res = g_strdup_printf ("tempuid-%lx-%d",
			       (gulong) time (NULL),
			       counter++);
	G_UNLOCK (lock);

	return res;
}

static void
imap_append_offline (CamelFolder *folder, CamelMimeMessage *message,
		     const CamelMessageInfo *info, gchar **appended_uid,
		     CamelException *ex)
{
	CamelImapMessageCache *cache = CAMEL_IMAP_FOLDER (folder)->cache;
	CamelFolderChangeInfo *changes;
	gchar *uid;

	uid = get_temp_uid ();

	camel_imap_summary_add_offline (folder->summary, uid, message, info);
	CAMEL_IMAP_FOLDER_REC_LOCK (folder, cache_lock);
	camel_imap_message_cache_insert_wrapper (cache, uid, "",
						 CAMEL_DATA_WRAPPER (message), ex);
	CAMEL_IMAP_FOLDER_REC_UNLOCK (folder, cache_lock);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, uid);
	camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed",
				    changes);
	camel_folder_change_info_free (changes);

	camel_imap_journal_log (CAMEL_IMAP_FOLDER (folder)->journal,
			       CAMEL_IMAP_JOURNAL_ENTRY_APPEND, uid);
	if (appended_uid)
		*appended_uid = uid;
	else
		g_free (uid);
}

static void
imap_folder_add_ignore_recent (CamelImapFolder *imap_folder, const gchar *uid)
{
	g_return_if_fail (imap_folder != NULL);
	g_return_if_fail (uid != NULL);

	if (!imap_folder->priv->ignore_recent)
		imap_folder->priv->ignore_recent = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);

	g_hash_table_insert (imap_folder->priv->ignore_recent, g_strdup (uid), GINT_TO_POINTER (1));
}

static gboolean
imap_folder_uid_in_ignore_recent (CamelImapFolder *imap_folder, const gchar *uid)
{
	g_return_val_if_fail (imap_folder != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	return imap_folder->priv->ignore_recent && g_hash_table_lookup (imap_folder->priv->ignore_recent, uid);
}

static CamelImapResponse *
do_append (CamelFolder *folder, CamelMimeMessage *message,
	   const CamelMessageInfo *info, gchar **uid,
	   CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response, *response2;
	CamelStream *memstream;
	CamelMimeFilter *crlf_filter;
	CamelStreamFilter *streamfilter;
	GByteArray *ba;
	gchar *flagstr, *end;
	guint32 flags = 0;

	/* encode any 8bit parts so we avoid sending embedded nul-chars and such  */
	camel_mime_message_encode_8bit_parts (message);

	/* FIXME: We could avoid this if we knew how big the message was. */
	memstream = camel_stream_mem_new ();
	ba = g_byte_array_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (memstream), ba);

	streamfilter = camel_stream_filter_new_with_stream (memstream);
	crlf_filter = camel_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_ENCODE,
						  CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
	camel_stream_filter_add (streamfilter, crlf_filter);
	camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message),
					    CAMEL_STREAM (streamfilter));
	camel_object_unref (CAMEL_OBJECT (streamfilter));
	camel_object_unref (CAMEL_OBJECT (crlf_filter));
	camel_object_unref (CAMEL_OBJECT (memstream));

	/* Some servers don't let us append with (CamelMessageInfo *)custom flags.  If the command fails for
	   whatever reason, assume this is the case and save the state and try again */
retry:
	if (info) {
		flags = camel_message_info_flags(info);
	}

	flags &= folder->permanent_flags;
	if (flags)
		flagstr = imap_create_flag_list (flags, (CamelMessageInfo *)info, folder->permanent_flags);
	else
		flagstr = NULL;

	response = camel_imap_command (store, NULL, ex, "APPEND %F%s%s {%d}",
				       folder->full_name, flagstr ? " " : "",
				       flagstr ? flagstr : "", ba->len);
	g_free (flagstr);

	if (!response) {
		if (camel_exception_get_id(ex) == CAMEL_EXCEPTION_SERVICE_INVALID && !store->nocustomappend) {
			camel_exception_clear(ex);
			store->nocustomappend = 1;
			goto retry;
		}
		g_byte_array_free (ba, TRUE);
		return NULL;
	}

	if (*response->status != '+') {
		if (!camel_exception_is_set (ex))
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Unexpected response status '%s' after APPEND command"), response->status);

		camel_imap_response_free (store, response);
		g_byte_array_free (ba, TRUE);
		return NULL;
	}

	/* send the rest of our data - the mime message */
	response2 = camel_imap_command_continuation (store, (const gchar *) ba->data, ba->len, ex);
	g_byte_array_free (ba, TRUE);

	/* free it only after message is sent. This may cause more FETCHes. */
	camel_imap_response_free (store, response);
	if (!response2) {
		if (!camel_exception_is_set (ex))
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("No response on continuation after APPEND command"));

		return response2;
	}

	if (store->capabilities & IMAP_CAPABILITY_UIDPLUS) {
		*uid = camel_strstrcase (response2->status, "[APPENDUID ");
		if (*uid)
			*uid = strchr (*uid + 11, ' ');
		if (*uid) {
			*uid = g_strndup (*uid + 1, strcspn (*uid + 1, "]"));
			/* Make sure it's a number */
			if (strtoul (*uid, &end, 10) == 0 || *end) {
				g_free (*uid);
				*uid = NULL;
			}
		}
	} else
		*uid = NULL;

	if (*uid)
		imap_folder_add_ignore_recent (CAMEL_IMAP_FOLDER (folder), *uid);

	return response2;
}

static void
imap_append_online (CamelFolder *folder, CamelMimeMessage *message,
		    const CamelMessageInfo *info, gchar **appended_uid,
		    CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	gchar *uid;
	gint count;

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		imap_append_offline (folder, message, info, appended_uid, ex);
		return;
	}

	count = camel_folder_summary_count (folder->summary);
	response = do_append (folder, message, info, &uid, ex);
	if (!response) {
		if (!camel_exception_is_set (ex))
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Unknown error occurred during APPEND command!"));
		return;
	}

	if (uid) {
		/* Cache first, since freeing response may trigger a
		 * summary update that will want this information.
		 */
		CAMEL_IMAP_FOLDER_REC_LOCK (folder, cache_lock);
		camel_imap_message_cache_insert_wrapper (
			CAMEL_IMAP_FOLDER (folder)->cache, uid,
			"", CAMEL_DATA_WRAPPER (message), ex);
		CAMEL_IMAP_FOLDER_REC_UNLOCK (folder, cache_lock);
		if (appended_uid)
			*appended_uid = uid;
		else
			g_free (uid);
	} else if (appended_uid)
		*appended_uid = NULL;

	camel_imap_response_free (store, response);

	/* Make sure a "folder_changed" is emitted. */
	CAMEL_SERVICE_REC_LOCK (store, connect_lock);
	if (store->current_folder != folder ||
	    camel_folder_summary_count (folder->summary) == count)
		imap_refresh_info (folder, ex);
	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
}

void
imap_append_resyncing (CamelFolder *folder, CamelMimeMessage *message,
		       const CamelMessageInfo *info, gchar **appended_uid,
		       CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	gchar *uid;

	response = do_append (folder, message, info, &uid, ex);
	if (!response)
		return;

	if (uid) {
		CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
		const gchar *olduid = camel_message_info_uid (info);

		CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);
		camel_imap_message_cache_copy (imap_folder->cache, olduid,
					       imap_folder->cache, uid, ex);
		CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);

		if (appended_uid)
			*appended_uid = uid;
		else
			g_free (uid);
	} else if (appended_uid)
		*appended_uid = NULL;

	camel_imap_response_free (store, response);
}

static void
imap_transfer_offline (CamelFolder *source, GPtrArray *uids,
		       CamelFolder *dest, GPtrArray **transferred_uids,
		       gboolean delete_originals, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (source->parent_store);
	CamelImapMessageCache *sc = CAMEL_IMAP_FOLDER (source)->cache;
	CamelImapMessageCache *dc = CAMEL_IMAP_FOLDER (dest)->cache;
	CamelFolderChangeInfo *changes;
	CamelMimeMessage *message;
	CamelMessageInfo *mi;
	gchar *uid, *destuid;
	gint i;

	/* We grab the store's command lock first, and then grab the
	 * source and destination cache_locks. This way we can't
	 * deadlock in the case where we're simultaneously also trying
	 * to copy messages in the other direction from another thread.
	 */
	CAMEL_SERVICE_REC_LOCK (store, connect_lock);
	CAMEL_IMAP_FOLDER_REC_LOCK (source, cache_lock);
	CAMEL_IMAP_FOLDER_REC_LOCK (dest, cache_lock);
	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

	if (transferred_uids) {
		*transferred_uids = g_ptr_array_new ();
		g_ptr_array_set_size (*transferred_uids, uids->len);
	}

	changes = camel_folder_change_info_new ();

	for (i = 0; i < uids->len && !camel_exception_is_set (ex); i++) {
		uid = uids->pdata[i];

		destuid = get_temp_uid ();

		mi = camel_folder_summary_uid (source->summary, uid);
		g_return_if_fail (mi != NULL);

		message = camel_folder_get_message (source, uid, NULL);

		if (message) {
			camel_imap_summary_add_offline (dest->summary, destuid, message, mi);
			camel_object_unref (CAMEL_OBJECT (message));
		} else
			camel_imap_summary_add_offline_uncached (dest->summary, destuid, mi);

		camel_imap_message_cache_copy (sc, uid, dc, destuid, ex);
		camel_message_info_free(mi);

		camel_folder_change_info_add_uid (changes, destuid);
		if (transferred_uids)
			(*transferred_uids)->pdata[i] = destuid;
		else
			g_free (destuid);

		if (delete_originals && !camel_exception_is_set (ex))
			camel_folder_delete_message (source, uid);
	}

	CAMEL_IMAP_FOLDER_REC_UNLOCK (dest, cache_lock);
	CAMEL_IMAP_FOLDER_REC_UNLOCK (source, cache_lock);

	camel_object_trigger_event (CAMEL_OBJECT (dest), "folder_changed", changes);
	camel_folder_change_info_free (changes);

	camel_imap_journal_log (CAMEL_IMAP_FOLDER (source)->journal, CAMEL_IMAP_JOURNAL_ENTRY_TRANSFER, dest,
						uids, delete_originals, ex);
}

static void
handle_copyuid (CamelImapResponse *response, CamelFolder *source,
		CamelFolder *destination)
{
	CamelImapMessageCache *scache = CAMEL_IMAP_FOLDER (source)->cache;
	CamelImapMessageCache *dcache = CAMEL_IMAP_FOLDER (destination)->cache;
	gchar *validity, *srcset, *destset;
	GPtrArray *src, *dest;
	gint i;

	validity = camel_strstrcase (response->status, "[COPYUID ");
	if (!validity)
		return;
	validity += 9;
	if (strtoul (validity, NULL, 10) !=
	    CAMEL_IMAP_SUMMARY (destination->summary)->validity)
		return;

	srcset = strchr (validity, ' ');
	if (!srcset++)
		goto lose;
	destset = strchr (srcset, ' ');
	if (!destset++)
		goto lose;

	src = imap_uid_set_to_array (source->summary, srcset);
	dest = imap_uid_set_to_array (destination->summary, destset);

	if (src && dest && src->len == dest->len) {
		/* We don't have to worry about deadlocking on the
		 * cache locks here, because we've got the store's
		 * command lock too, so no one else could be here.
		 */
		CAMEL_IMAP_FOLDER_REC_LOCK (source, cache_lock);
		CAMEL_IMAP_FOLDER_REC_LOCK (destination, cache_lock);
		for (i = 0; i < src->len; i++) {
			camel_imap_message_cache_copy (scache, src->pdata[i],
						       dcache, dest->pdata[i],
						       NULL);

			imap_folder_add_ignore_recent (CAMEL_IMAP_FOLDER (destination), dest->pdata[i]);
		}
		CAMEL_IMAP_FOLDER_REC_UNLOCK (source, cache_lock);
		CAMEL_IMAP_FOLDER_REC_UNLOCK (destination, cache_lock);

		imap_uid_array_free (src);
		imap_uid_array_free (dest);
		return;
	}

	if (src)
		imap_uid_array_free (src);
	if (dest)
		imap_uid_array_free (dest);
 lose:
	g_warning ("Bad COPYUID response from server");
}

static void
handle_copyuid_copy_user_tags (CamelImapResponse *response, CamelFolder *source, CamelFolder *destination)
{
	gchar *validity, *srcset, *destset;
	GPtrArray *src, *dest;
	gint i;
	CamelException ex;

	validity = camel_strstrcase (response->status, "[COPYUID ");
	if (!validity)
		return;
	validity += 9;
	if (strtoul (validity, NULL, 10) !=
	    CAMEL_IMAP_SUMMARY (destination->summary)->validity)
		return;

	srcset = strchr (validity, ' ');
	if (!srcset++)
		goto lose;
	destset = strchr (srcset, ' ');
	if (!destset++)
		goto lose;

	/* first do NOOP on the destination folder, so server has enough time to propagate our copy command there */
	camel_imap_response_free (CAMEL_IMAP_STORE (destination->parent_store),
				  camel_imap_command (CAMEL_IMAP_STORE (destination->parent_store), destination, NULL, "NOOP"));

	camel_exception_init (&ex);
	/* refresh folder's summary first, we copied messages there on the server,
	   but do not know about it in a local summary */
	imap_refresh_info (destination, &ex);
	if (camel_exception_is_set (&ex)) {
		g_warning ("destination folder refresh failed, error: %s", ex.desc);
		camel_exception_clear (&ex);
		goto lose;
	}
	camel_exception_clear (&ex);

	src = imap_uid_set_to_array (source->summary, srcset);
	dest = imap_uid_set_to_array (destination->summary, destset);

	if (src && dest && src->len == dest->len) {
		/* We don't have to worry about deadlocking on the
		 * cache locks here, because we've got the store's
		 * command lock too, so no one else could be here.
		 */
		CAMEL_IMAP_FOLDER_REC_LOCK (source, cache_lock);
		CAMEL_IMAP_FOLDER_REC_LOCK (destination, cache_lock);
		for (i = 0; i < src->len; i++) {
			CamelMessageInfo *mi = camel_folder_get_message_info (source, src->pdata[i]);

			if (mi) {
				const CamelTag *tag = camel_message_info_user_tags (mi);

				while (tag) {
					camel_folder_set_message_user_tag (destination, dest->pdata[i], tag->name, tag->value);
					tag = tag->next;
				}

				camel_folder_free_message_info (source, mi);
			}
		}
		CAMEL_IMAP_FOLDER_REC_UNLOCK (source, cache_lock);
		CAMEL_IMAP_FOLDER_REC_UNLOCK (destination, cache_lock);

		imap_uid_array_free (src);
		imap_uid_array_free (dest);
		return;
	}

	if (src)
		imap_uid_array_free (src);
	if (dest)
		imap_uid_array_free (dest);
 lose:
	g_warning ("Bad COPYUID response from server");
}

/* returns whether any of messages from uidset has set any user tag or not */
static gboolean
any_has_user_tag (CamelFolder *source, gchar *uidset)
{
	GPtrArray *src;

	g_return_val_if_fail (source != NULL && uidset != NULL, FALSE);

	src = imap_uid_set_to_array (source->summary, uidset);
	if (src) {
		gboolean have = FALSE;
		gint i;

		CAMEL_IMAP_FOLDER_REC_LOCK (source, cache_lock);
		for (i = 0; i < src->len && !have; i++) {
			CamelMessageInfo *mi = camel_folder_get_message_info (source, src->pdata[i]);

			if (mi) {
				have = camel_message_info_user_tags (mi) != NULL;

				camel_folder_free_message_info (source, mi);
			}
		}
		CAMEL_IMAP_FOLDER_REC_UNLOCK (source, cache_lock);

		imap_uid_array_free (src);

		return have;
	}

	return FALSE;
}

static void
do_copy (CamelFolder *source, GPtrArray *uids,
	 CamelFolder *destination, gint delete_originals, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (source->parent_store);
	CamelImapResponse *response;
	gchar *uidset;
	gint uid = 0, last=0, i;

	while (uid < uids->len && !camel_exception_is_set (ex)) {
		uidset = imap_uid_array_to_set (source->summary, uids, uid, UID_SET_LIMIT, &uid);

		/* use XGWMOVE only when none of the moving messages has set any user tag */
		if ((store->capabilities & IMAP_CAPABILITY_XGWMOVE) != 0 && delete_originals && !any_has_user_tag (source, uidset)) {
			response = camel_imap_command (store, source, ex, "UID XGWMOVE %s %F", uidset, destination->full_name);
			/* returns only 'A00012 OK UID XGWMOVE completed' '* 2 XGWMOVE' so nothing useful */
			camel_imap_response_free (store, response);
		} else {
			response = camel_imap_command (store, source, ex, "UID COPY %s %F", uidset, destination->full_name);
			if (response && (store->capabilities & IMAP_CAPABILITY_UIDPLUS))
				handle_copyuid (response, source, destination);
			if (response)
				handle_copyuid_copy_user_tags (response, source, destination);
			camel_imap_response_free (store, response);
		}

		if (!camel_exception_is_set(ex) && delete_originals) {
			for (i=last;i<uid;i++)
				camel_folder_delete_message(source, uids->pdata[i]);
			last = uid;
		}
		g_free (uidset);
	}
}

static void
imap_transfer_online (CamelFolder *source, GPtrArray *uids,
		      CamelFolder *dest, GPtrArray **transferred_uids,
		      gboolean delete_originals, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (source->parent_store);
	gint count;

	if (CAMEL_OFFLINE_STORE (store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		imap_transfer_offline (source, uids, dest, transferred_uids, delete_originals, ex);
		return;
	}

	/* Sync message flags if needed. */
	imap_sync (source, FALSE, ex);
	if (camel_exception_is_set (ex))
		return;

	count = camel_folder_summary_count (dest->summary);

	qsort (uids->pdata, uids->len, sizeof (gpointer), uid_compar);

	/* Now copy the messages */
	do_copy(source, uids, dest, delete_originals, ex);
	if (camel_exception_is_set (ex))
		return;

	/* Make the destination notice its new messages */
	if (store->current_folder != dest ||
	    camel_folder_summary_count (dest->summary) == count)
		imap_refresh_info (dest, ex);

	/* FIXME */
	if (transferred_uids)
		*transferred_uids = NULL;
}

void
imap_transfer_resyncing (CamelFolder *source, GPtrArray *uids,
			 CamelFolder *dest, GPtrArray **transferred_uids,
			 gboolean delete_originals, CamelException *ex)
{
	GPtrArray *realuids;
	gint first, i;
	const gchar *uid;
	CamelMimeMessage *message;
	CamelMessageInfo *info;

	qsort (uids->pdata, uids->len, sizeof (gpointer), uid_compar);

	/*This is trickier than append_resyncing, because some of
	 * the messages we are copying may have been copied or
	 * appended into @source while we were offline, in which case
	 * if we don't have UIDPLUS, we won't know their real UIDs,
	 * so we'll have to append them rather than copying. */

	realuids = g_ptr_array_new ();

	i = 0;
	while (i < uids->len && !camel_exception_is_set (ex)) {
		 /* Skip past real UIDs  */
		for (first = i; i < uids->len; i++) {
			uid = uids->pdata[i];

			if (!isdigit ((guchar)*uid)) {
				uid = camel_imap_journal_uidmap_lookup ((CamelIMAPJournal *) CAMEL_IMAP_FOLDER (source)->journal, uid);
				if (!uid)
					break;
			}
			g_ptr_array_add (realuids, (gchar *)uid);
		}

		/* If we saw any real UIDs, do a COPY */
		if (i != first) {
			do_copy (source, realuids, dest, delete_originals, ex);
			g_ptr_array_set_size (realuids, 0);
			if (i == uids->len || camel_exception_is_set (ex))
				break;
		}

		/* Deal with fake UIDs */
		while (i < uids->len &&
		       !isdigit (*(guchar *)(uids->pdata[i])) &&
		       !camel_exception_is_set (ex)) {
			uid = uids->pdata[i];
			message = camel_folder_get_message (source, uid, NULL);
			if (!message) {
				/* Message must have been expunged */
				i++;
				continue;
			}
			info = camel_folder_get_message_info (source, uid);
			g_return_if_fail (info != NULL);

			imap_append_online (dest, message, info, NULL, ex);
			camel_folder_free_message_info (source, info);
			camel_object_unref (CAMEL_OBJECT (message));
			if (delete_originals && !camel_exception_is_set (ex))
				camel_folder_delete_message (source, uid);
			i++;
		}
	}

	g_ptr_array_free (realuids, FALSE);

	/* FIXME */
	if (transferred_uids)
		*transferred_uids = NULL;
}

static GPtrArray *
imap_search_by_expression (CamelFolder *folder, const gchar *expression, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	GPtrArray *matches;

	/* we could get around this by creating a new search object each time,
	   but i doubt its worth it since any long operation would lock the
	   command channel too */
	CAMEL_IMAP_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_set_folder (imap_folder->search, folder);
	matches = camel_folder_search_search(imap_folder->search, expression, NULL, ex);

	CAMEL_IMAP_FOLDER_UNLOCK(folder, search_lock);

	return matches;
}

static guint32
imap_count_by_expression (CamelFolder *folder, const gchar *expression, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	guint32 matches;

	/* we could get around this by creating a new search object each time,
	   but i doubt its worth it since any long operation would lock the
	   command channel too */
	CAMEL_IMAP_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_set_folder (imap_folder->search, folder);
	matches = camel_folder_search_count(imap_folder->search, expression, ex);

	CAMEL_IMAP_FOLDER_UNLOCK(folder, search_lock);

	return matches;
}
static GPtrArray *
imap_search_by_uids(CamelFolder *folder, const gchar *expression, GPtrArray *uids, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER(folder);
	GPtrArray *matches;

	if (uids->len == 0)
		return g_ptr_array_new();

	CAMEL_IMAP_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_set_folder(imap_folder->search, folder);
	matches = camel_folder_search_search(imap_folder->search, expression, uids, ex);

	CAMEL_IMAP_FOLDER_UNLOCK(folder, search_lock);

	return matches;
}

static void
imap_search_free (CamelFolder *folder, GPtrArray *uids)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);

	g_return_if_fail (imap_folder->search);

	CAMEL_IMAP_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_free_result (imap_folder->search, uids);

	CAMEL_IMAP_FOLDER_UNLOCK(folder, search_lock);
}

static CamelMimeMessage *get_message (CamelImapFolder *imap_folder,
				      const gchar *uid,
				      CamelMessageContentInfo *ci,
				      CamelException *ex);

struct _part_spec_stack {
	struct _part_spec_stack *parent;
	gint part;
};

static void
part_spec_push (struct _part_spec_stack **stack, gint part)
{
	struct _part_spec_stack *node;

	node = g_new (struct _part_spec_stack, 1);
	node->parent = *stack;
	node->part = part;

	*stack = node;
}

static gint
part_spec_pop (struct _part_spec_stack **stack)
{
	struct _part_spec_stack *node;
	gint part;

	g_return_val_if_fail (*stack != NULL, 0);

	node = *stack;
	*stack = node->parent;

	part = node->part;
	g_free (node);

	return part;
}

static gchar *
content_info_get_part_spec (CamelMessageContentInfo *ci)
{
	struct _part_spec_stack *stack = NULL;
	CamelMessageContentInfo *node;
	gchar *part_spec, *buf;
	gsize len = 1;
	gint part;

	node = ci;
	while (node->parent) {
		CamelMessageContentInfo *child;

		/* FIXME: is this only supposed to apply if 'node' is a multipart? */
		if (node->parent->parent &&
				camel_content_type_is (node->parent->type, "message", "*") &&
				!camel_content_type_is (node->parent->parent->type, "message", "*")) {
			node = node->parent;
			continue;
		}

		child = node->parent->childs;
		for (part = 1; child; part++) {
			if (child == node)
				break;

			child = child->next;
		}

		part_spec_push (&stack, part);

		len += 2;
		while ((part = part / 10))
			len++;

		node = node->parent;
	}

	buf = part_spec = g_malloc (len);
	part_spec[0] = '\0';

	while (stack) {
		part = part_spec_pop (&stack);
		buf += sprintf (buf, "%d%s", part, stack ? "." : "");
	}

	return part_spec;
}

/* Fetch the contents of the MIME part indicated by @ci, which is part
 * of message @uid in @folder.
 */
static CamelDataWrapper *
get_content (CamelImapFolder *imap_folder, const gchar *uid,
	     CamelMimePart *part, CamelMessageContentInfo *ci,
	     gint frommsg,
	     CamelException *ex)
{
	CamelDataWrapper *content = NULL;
	CamelStream *stream;
	gchar *part_spec;

	part_spec = content_info_get_part_spec (ci);

	d(printf("get content '%s' '%s' (frommsg = %d)\n", part_spec, camel_content_type_format(ci->type), frommsg));

	/* There are three cases: multipart/signed, multipart, message/rfc822, and "other" */
	if (camel_content_type_is (ci->type, "multipart", "signed")) {
		CamelMultipartSigned *body_mp;
		gchar *spec;
		gint ret;

		/* Note: because we get the content parts uninterpreted anyway, we could potentially
		   just use the normalmultipart code, except that multipart/signed wont let you yet! */

		body_mp = camel_multipart_signed_new ();
		/* need to set this so it grabs the boundary and other info about the signed type */
		/* we assume that part->content_type is more accurate/full than ci->type */
		camel_data_wrapper_set_mime_type_field (CAMEL_DATA_WRAPPER (body_mp), CAMEL_DATA_WRAPPER (part)->mime_type);

		spec = g_alloca(strlen(part_spec) + 6);
		if (frommsg)
			sprintf(spec, part_spec[0] ? "%s.TEXT" : "TEXT", part_spec);
		else
			strcpy(spec, part_spec);
		g_free(part_spec);

		stream = camel_imap_folder_fetch_data (imap_folder, uid, spec, FALSE, ex);
		if (stream) {
			ret = camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (body_mp), stream);
			camel_object_unref (CAMEL_OBJECT (stream));
			if (ret == -1) {
				camel_object_unref ((CamelObject *) body_mp);
				return NULL;
			}
		}

		return (CamelDataWrapper *) body_mp;
	} else if (camel_content_type_is (ci->type, "multipart", "*")) {
		CamelMultipart *body_mp;
		gchar *child_spec;
		gint speclen, num, isdigest;

		if (camel_content_type_is (ci->type, "multipart", "encrypted"))
			body_mp = (CamelMultipart *) camel_multipart_encrypted_new ();
		else
			body_mp = camel_multipart_new ();

		/* need to set this so it grabs the boundary and other info about the multipart */
		/* we assume that part->content_type is more accurate/full than ci->type */
		camel_data_wrapper_set_mime_type_field (CAMEL_DATA_WRAPPER (body_mp), CAMEL_DATA_WRAPPER (part)->mime_type);
		isdigest = camel_content_type_is(((CamelDataWrapper *)part)->mime_type, "multipart", "digest");

		speclen = strlen (part_spec);
		child_spec = g_malloc (speclen + 17); /* dot + 10 + dot + MIME + nul */
		memcpy (child_spec, part_spec, speclen);
		if (speclen > 0)
			child_spec[speclen++] = '.';
		g_free (part_spec);

		ci = ci->childs;
		num = 1;
		while (ci) {
			sprintf (child_spec + speclen, "%d.MIME", num++);
			stream = camel_imap_folder_fetch_data (imap_folder, uid, child_spec, FALSE, ex);
			if (stream) {
				gint ret;

				part = camel_mime_part_new ();
				ret = camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (part), stream);
				camel_object_unref (CAMEL_OBJECT (stream));
				if (ret == -1) {
					camel_object_unref (CAMEL_OBJECT (part));
					camel_object_unref (CAMEL_OBJECT (body_mp));
					g_free (child_spec);
					return NULL;
				}

				content = get_content (imap_folder, uid, part, ci, FALSE, ex);
			}

			if (!stream || !content) {
				camel_object_unref (CAMEL_OBJECT (body_mp));
				g_free (child_spec);
				return NULL;
			}

			if (camel_debug("imap:folder")) {
				gchar *ct = camel_content_type_format(camel_mime_part_get_content_type((CamelMimePart *)part));
				gchar *ct2 = camel_content_type_format(ci->type);

				printf("Setting part content type to '%s' contentinfo type is '%s'\n", ct, ct2);
				g_free(ct);
				g_free(ct2);
			}

			/* if we had no content-type header on a multipart/digest sub-part, then we need to
			   treat it as message/rfc822 instead */
			if (isdigest && camel_medium_get_header((CamelMedium *)part, "content-type") == NULL) {
				CamelContentType *ct = camel_content_type_new("message", "rfc822");

				camel_data_wrapper_set_mime_type_field(content, ct);
				camel_content_type_unref(ct);
			} else {
				camel_data_wrapper_set_mime_type_field(content, camel_mime_part_get_content_type(part));
			}

			camel_medium_set_content_object (CAMEL_MEDIUM (part), content);
			camel_object_unref(content);

			camel_multipart_add_part (body_mp, part);
			camel_object_unref(part);

			ci = ci->next;
		}

		g_free (child_spec);

		return (CamelDataWrapper *) body_mp;
	} else if (camel_content_type_is (ci->type, "message", "rfc822")) {
		content = (CamelDataWrapper *) get_message (imap_folder, uid, ci->childs, ex);
		g_free (part_spec);
		return content;
	} else {
		CamelTransferEncoding enc;
		gchar *spec;

		/* NB: we need this differently to multipart/signed case above on purpose */
		spec = g_alloca(strlen(part_spec) + 6);
		if (frommsg)
			sprintf(spec, part_spec[0] ? "%s.1" : "1", part_spec);
		else
			strcpy(spec, part_spec[0]?part_spec:"1");

		enc = ci->encoding?camel_transfer_encoding_from_string(ci->encoding):CAMEL_TRANSFER_ENCODING_DEFAULT;
		content = camel_imap_wrapper_new (imap_folder, ci->type, enc, uid, spec, part);
		g_free (part_spec);
		return content;
	}
}

static CamelMimeMessage *
get_message (CamelImapFolder *imap_folder, const gchar *uid,
	     CamelMessageContentInfo *ci,
	     CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (CAMEL_FOLDER (imap_folder)->parent_store);
	CamelDataWrapper *content;
	CamelMimeMessage *msg;
	CamelStream *stream;
	gchar *section_text, *part_spec;
	gint ret;

	part_spec = content_info_get_part_spec(ci);
	d(printf("get message '%s'\n", part_spec));
	section_text = g_strdup_printf ("%s%s%s", part_spec, *part_spec ? "." : "",
					store->server_level >= IMAP_LEVEL_IMAP4REV1 ? "HEADER" : "0");

	stream = camel_imap_folder_fetch_data (imap_folder, uid, section_text, FALSE, ex);
	g_free (section_text);
	g_free(part_spec);
	if (!stream)
		return NULL;

	msg = camel_mime_message_new ();
	ret = camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg), stream);
	camel_object_unref (CAMEL_OBJECT (stream));
	if (ret == -1) {
		camel_object_unref (CAMEL_OBJECT (msg));
		return NULL;
	}

	content = get_content (imap_folder, uid, CAMEL_MIME_PART (msg), ci, TRUE, ex);
	if (!content) {
		camel_object_unref (CAMEL_OBJECT (msg));
		return NULL;
	}

	if (camel_debug("imap:folder")) {
		gchar *ct = camel_content_type_format(camel_mime_part_get_content_type((CamelMimePart *)msg));
		gchar *ct2 = camel_content_type_format(ci->type);

		printf("Setting message content type to '%s' contentinfo type is '%s'\n", ct, ct2);
		g_free(ct);
		g_free(ct2);
	}

	camel_data_wrapper_set_mime_type_field(content, camel_mime_part_get_content_type((CamelMimePart *)msg));
	camel_medium_set_content_object (CAMEL_MEDIUM (msg), content);
	camel_object_unref (CAMEL_OBJECT (content));

	return msg;
}

#define IMAP_SMALL_BODY_SIZE 5120

static CamelMimeMessage *
get_message_simple (CamelImapFolder *imap_folder, const gchar *uid,
		    CamelStream *stream, CamelException *ex)
{
	CamelMimeMessage *msg;
	gint ret;

	if (!stream) {
		stream = camel_imap_folder_fetch_data (imap_folder, uid, "",
						       FALSE, ex);
		if (!stream)
			return NULL;
	}

	msg = camel_mime_message_new ();
	ret = camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg),
							stream);
	camel_object_unref (CAMEL_OBJECT (stream));
	if (ret == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Unable to retrieve message: %s"),
				      g_strerror (errno));
		camel_object_unref (CAMEL_OBJECT (msg));
		return NULL;
	}

	return msg;
}

static gboolean
content_info_incomplete (CamelMessageContentInfo *ci)
{
	if (!ci->type)
		return TRUE;

	if (camel_content_type_is (ci->type, "multipart", "*")
	    || camel_content_type_is (ci->type, "message", "rfc822")) {
		if (!ci->childs)
			return TRUE;
		for (ci = ci->childs;ci;ci=ci->next)
			if (content_info_incomplete(ci))
				return TRUE;
	}

	return FALSE;
}

static CamelImapMessageInfo *
imap_folder_summary_uid_or_error(CamelFolderSummary *summary, const gchar * uid, CamelException *ex)
{
	CamelImapMessageInfo *mi;
	mi = (CamelImapMessageInfo *)camel_folder_summary_uid (summary, uid);
	if (mi == NULL) {
		camel_exception_setv (
			ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
			_("Cannot get message with message ID %s: %s"),
			uid, _("No such message available."));
	}
	return mi;
}

static CamelMimeMessage *
imap_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapMessageInfo *mi;
	CamelMimeMessage *msg = NULL;
	CamelStream *stream = NULL;
	gint retry;

	mi = imap_folder_summary_uid_or_error(folder->summary, uid, ex);
	if (!mi)
	  return NULL;

	/* If its cached in full, just get it as is, this is only a shortcut,
	   since we get stuff from the cache anyway.  It affects a busted connection though. */
	if ( (stream = camel_imap_folder_fetch_data(imap_folder, uid, "", TRUE, NULL))
	     && (msg = get_message_simple(imap_folder, uid, stream, ex)))
		goto done;

	/* All this mess is so we silently retry a fetch if we fail with
	   service_unavailable, without an (equivalent) mess of gotos */
	retry = 0;
	do {
		retry++;
		camel_exception_clear(ex);

		/* If the message is small or only 1 part, or server doesn't do 4v1 (properly) fetch it in one piece. */
		if (store->server_level < IMAP_LEVEL_IMAP4REV1
		    || store->braindamaged
		    || mi->info.size < IMAP_SMALL_BODY_SIZE
		    || (!content_info_incomplete(mi->info.content) && !mi->info.content->childs)) {
			CamelMessageInfoBase *info = (CamelMessageInfoBase *) camel_folder_summary_uid (folder->summary, uid);
			msg = get_message_simple (imap_folder, uid, NULL, ex);
			if (info && !info->preview && msg && camel_folder_summary_get_need_preview(folder->summary)) {
				if (camel_mime_message_build_preview ((CamelMimePart *)msg, (CamelMessageInfo *)info) && info->preview)
					camel_folder_summary_add_preview (folder->summary, (CamelMessageInfo *)info);
			}

			camel_message_info_free (info);
		} else {
			if (content_info_incomplete (mi->info.content)) {
				/* For larger messages, fetch the structure and build a message
				 * with offline parts. (We check mi->content->type rather than
				 * mi->content because camel_folder_summary_info_new always creates
				 * an empty content struct.)
				 */
				CamelImapResponse *response;
				GData *fetch_data = NULL;
				gchar *body, *found_uid;
				gint i;

				CAMEL_SERVICE_REC_LOCK(store, connect_lock);
				if (!camel_imap_store_connected(store, ex)) {
					CAMEL_SERVICE_REC_UNLOCK(store, connect_lock);
					camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
							     _("This message is not currently available"));
					goto fail;
				}

				response = camel_imap_command (store, folder, ex, "UID FETCH %s BODY", uid);
				CAMEL_SERVICE_REC_UNLOCK(store, connect_lock);

				if (response) {
					for (i = 0, body = NULL; i < response->untagged->len; i++) {
						fetch_data = parse_fetch_response (imap_folder, response->untagged->pdata[i]);
						if (fetch_data) {
							found_uid = g_datalist_get_data (&fetch_data, "UID");
							body = g_datalist_get_data (&fetch_data, "BODY");
							if (found_uid && body && !strcmp (found_uid, uid))
								break;
							g_datalist_clear (&fetch_data);
							fetch_data = NULL;
							body = NULL;
						}
					}

					if (body) {
						/* NB: small race here, setting the info.content */
						imap_parse_body ((const gchar **) &body, folder, mi->info.content);
						mi->info.dirty = TRUE;
						camel_folder_summary_touch (folder->summary);
					}

					if (fetch_data)
						g_datalist_clear (&fetch_data);

					camel_imap_response_free (store, response);
				} else {
					camel_exception_clear(ex);
				}
			}

			if (camel_debug_start("imap:folder")) {
				printf("Folder get message '%s' folder info ->\n", uid);
				camel_message_info_dump((CamelMessageInfo *)mi);
				camel_debug_end();
			}

			/* FETCH returned OK, but we didn't parse a BODY
			 * response. Courier will return invalid BODY
			 * responses for invalidly MIMEd messages, so
			 * fall back to fetching the entire thing and
			 * let the mailer's "bad MIME" code handle it.
			 */
			if (content_info_incomplete (mi->info.content))
				msg = get_message_simple (imap_folder, uid, NULL, ex);
			else
				msg = get_message (imap_folder, uid, mi->info.content, ex);
			if (msg && camel_folder_summary_get_need_preview(folder->summary)) {
				CamelMessageInfoBase *info = (CamelMessageInfoBase *) camel_folder_summary_uid (folder->summary, uid);
				if (info && !info->preview) {
					if (camel_mime_message_build_preview ((CamelMimePart *)msg, (CamelMessageInfo *)info) && info->preview)
						camel_folder_summary_add_preview (folder->summary, (CamelMessageInfo *)info);
				}
				camel_message_info_free (info);
			}

		}
	} while (msg == NULL
		 && retry < 2
		 && camel_exception_get_id(ex) == CAMEL_EXCEPTION_SERVICE_UNAVAILABLE);

done:
	if (msg) {
		gboolean has_attachment;

		/* FIXME, this shouldn't be done this way. */
		camel_medium_set_header (CAMEL_MEDIUM (msg), "X-Evolution-Source", store->base_url);

		if (!mi->info.mlist || !*mi->info.mlist) {
			/* update mailing list information, if necessary */
			gchar *mlist = camel_header_raw_check_mailing_list (&(CAMEL_MIME_PART (msg)->headers));

			if (mlist) {
				if (mi->info.mlist)
					camel_pstring_free (mi->info.mlist);
				mi->info.mlist = camel_pstring_add (mlist, TRUE);
				mi->info.dirty = TRUE;

				if (mi->info.summary)
					camel_folder_summary_touch (mi->info.summary);
			}
		}

		has_attachment = camel_mime_message_has_attachment (msg);
		if (((mi->info.flags & CAMEL_MESSAGE_ATTACHMENTS) && !has_attachment) ||
		    ((mi->info.flags & CAMEL_MESSAGE_ATTACHMENTS) == 0 && has_attachment)) {
			if (has_attachment)
				mi->info.flags = mi->info.flags | CAMEL_MESSAGE_ATTACHMENTS;
			else
				mi->info.flags = mi->info.flags & ~CAMEL_MESSAGE_ATTACHMENTS;
			mi->info.dirty = TRUE;

			if (mi->info.summary)
				camel_folder_summary_touch (mi->info.summary);
		}
	}
fail:
	camel_message_info_free(&mi->info);

	return msg;
}

/**
 * imap_sync_message
 *
 * Ensure that a message is cached locally, but don't retrieve the content if
 * it is already local.
 */
static void
imap_sync_message (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapMessageInfo *mi;
	CamelMimeMessage *msg = NULL;
	CamelStream *stream = NULL;

	mi = imap_folder_summary_uid_or_error(folder->summary, uid, ex);
	if (!mi)
	  /* No such UID - is this duplicate work? The sync process selects
	   * UIDs to start with.
	   */
	  return;
	camel_message_info_free(&mi->info);

	/* If we can get a stream, assume its fully cached. This may be false
	 * if partial streams are saved elsewhere in the code - but that seems
	 * best solved by knowning more about whether a given message is fully
	 * available locally or not,
	 */
	/* If its cached in full, just get it as is, this is only a shortcut,
	   since we get stuff from the cache anyway.  It affects a busted connection though. */
	if ((stream = camel_imap_folder_fetch_data(imap_folder, uid, "", TRUE, NULL))) {
		camel_object_unref (stream);
		return;
	}
	msg = imap_get_message(folder, uid, ex);
	if (msg)
		camel_object_unref(msg);
}

/* FIXME Remove it after confirming
static void
imap_cache_message (CamelDiscoFolder *disco_folder, const gchar *uid,
		    CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (disco_folder);
	CamelStream *stream;

	stream = camel_imap_folder_fetch_data (imap_folder, uid, "", FALSE, ex);
	if (stream)
		camel_object_unref (CAMEL_OBJECT (stream));
}
*/

/* We pretend that a FLAGS or RFC822.SIZE response is always exactly
 * 20 bytes long, and a BODY[HEADERS] response is always 2000 bytes
 * long. Since we know how many of each kind of response we're
 * expecting, we can find the total (pretend) amount of server traffic
 * to expect and then count off the responses as we read them to update
 * the progress bar.
 */
#define IMAP_PRETEND_SIZEOF_FLAGS	  20
#define IMAP_PRETEND_SIZEOF_SIZE	  20
#define IMAP_PRETEND_SIZEOF_HEADERS	2000

static const gchar *tm_months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static gboolean
decode_time (const guchar **in, gint *hour, gint *min, gint *sec)
{
	register const guchar *inptr;
	gint *val, colons = 0;

	*hour = *min = *sec = 0;

	val = hour;
	for (inptr = *in; *inptr && !isspace ((gint) *inptr); inptr++) {
		if (*inptr == ':') {
			colons++;
			switch (colons) {
			case 1:
				val = min;
				break;
			case 2:
				val = sec;
				break;
			default:
				return FALSE;
			}
		} else if (!isdigit ((gint) *inptr))
			return FALSE;
		else
			*val = (*val * 10) + (*inptr - '0');
	}

	*in = inptr;

	return TRUE;
}

static time_t
decode_internaldate (const guchar *in)
{
	const guchar *inptr = in;
	gint hour, min, sec, n;
	guchar *buf;
	struct tm tm;
	time_t date;

	memset ((gpointer) &tm, 0, sizeof (struct tm));

	tm.tm_mday = strtoul ((gchar *) inptr, (gchar **) &buf, 10);
	if (buf == inptr || *buf != '-')
		return (time_t) -1;

	inptr = buf + 1;
	if (inptr[3] != '-')
		return (time_t) -1;

	for (n = 0; n < 12; n++) {
		if (!g_ascii_strncasecmp ((gchar *) inptr, tm_months[n], 3))
			break;
	}

	if (n >= 12)
		return (time_t) -1;

	tm.tm_mon = n;

	inptr += 4;

	n = strtoul ((gchar *) inptr, (gchar **) &buf, 10);
	if (buf == inptr || *buf != ' ')
		return (time_t) -1;

	tm.tm_year = n - 1900;

	inptr = buf + 1;
	if (!decode_time (&inptr, &hour, &min, &sec))
		return (time_t) -1;

	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec;

	n = strtol ((gchar *) inptr, NULL, 10);

	date = e_mktime_utc (&tm);

	/* date is now GMT of the time we want, but not offset by the timezone ... */

	/* this should convert the time to the GMT equiv time */
	date -= ((n / 100) * 60 * 60) + (n % 100) * 60;

	return date;
}

static void
add_message_from_data (CamelFolder *folder, GPtrArray *messages,
		       gint first, GData *data)
{
	CamelMimeMessage *msg;
	CamelStream *stream;
	CamelImapMessageInfo *mi;
	const gchar *idate;
	const gchar *bodystructure;
	gint seq;

	seq = GPOINTER_TO_INT (g_datalist_get_data (&data, "SEQUENCE"));
	if (seq < first)
		return;
	stream = g_datalist_get_data (&data, "BODY_PART_STREAM");
	if (!stream)
		return;

	if (seq - first >= messages->len)
		g_ptr_array_set_size (messages, seq - first + 1);

	msg = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg), stream) == -1) {
		camel_object_unref (CAMEL_OBJECT (msg));
		return;
	}

	bodystructure = g_datalist_get_data (&data, "BODY");

	mi = (CamelImapMessageInfo *)camel_folder_summary_info_new_from_message (folder->summary, msg, bodystructure);
	camel_object_unref (CAMEL_OBJECT (msg));

	if ((idate = g_datalist_get_data (&data, "INTERNALDATE")))
		mi->info.date_received = decode_internaldate ((const guchar *) idate);

	if (mi->info.date_received == -1)
		mi->info.date_received = mi->info.date_sent;

	messages->pdata[seq - first] = mi;
}

struct _junk_data {
	GData *data;
	CamelMessageInfoBase *mi;
};

static void
construct_junk_headers (gchar *header, gchar *value, struct _junk_data *jdata)
{
	gchar *bs, *es, *flag=NULL;
	gchar *bdata = g_datalist_get_data (&(jdata->data), "BODY_PART_DATA");
	struct _camel_header_param *node;

	/* FIXME: This can be written in a much clever way.
	 * We can create HEADERS file or carry all headers till filtering so
	 * that header based filtering can be much faster. But all that later. */
	bs = camel_strstrcase (bdata ? bdata:"", header);
	if (bs) {
		bs += strlen(header);
		bs = strchr (bs, ':');
		if (bs) {
			bs++;
			while (*bs == ' ')
				bs++;
			es = strchr (bs, '\n');
			if (es)
				flag = g_strndup (bs, es-bs);
			else
				bs = NULL;
		}

	}

	if (bs) {
		node = g_new (struct _camel_header_param, 1);
		node->name = g_strdup (header);
		node->value = flag;
		node->next = jdata->mi->headers;
		jdata->mi->headers = node;
	}
}

static void
update_summary (CamelFolderSummary *summary, CamelMessageInfoBase *info)
{
	gint unread=0, deleted=0, junk=0;
	guint32 flags = info->flags;

	if (!(flags & CAMEL_MESSAGE_SEEN))
		unread = 1;

	if (flags & CAMEL_MESSAGE_DELETED)
		deleted = 1;

	if (flags & CAMEL_MESSAGE_JUNK)
		junk = 1;

	if (summary) {

		if (unread)
			summary->unread_count += unread;
		if (deleted)
			summary->deleted_count += deleted;
		if (junk)
			summary->junk_count += junk;
		if (junk && !deleted)
			summary->junk_not_deleted_count += junk;
		summary->visible_count++;
		if (junk ||  deleted)
			summary->visible_count -= junk ? junk : deleted;

		summary->saved_count++;
		camel_folder_summary_touch(summary);
	}
}

#define CAMEL_MESSAGE_INFO_HEADERS "DATE FROM TO CC SUBJECT REFERENCES IN-REPLY-TO MESSAGE-ID MIME-VERSION CONTENT-TYPE CONTENT-CLASS X-CALENDAR-ATTACHMENT "

/* FIXME: this needs to be kept in sync with camel-mime-utils.c's list
   of mailing-list headers and so might be best if this were
   auto-generated? */
#define MAILING_LIST_HEADERS "X-MAILING-LIST X-LOOP LIST-ID LIST-POST MAILING-LIST ORIGINATOR X-LIST SENDER RETURN-PATH X-BEENTHERE "

static void
imap_update_summary (CamelFolder *folder, gint exists,
		     CamelFolderChangeInfo *changes,
		     CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	GPtrArray *fetch_data = NULL, *messages = NULL, *needheaders;
	guint32 flags, uidval;
	gint i, seq, first, size, got;
	CamelImapResponseType type;
	GString *header_spec = NULL;
	CamelImapMessageInfo *mi;
	CamelStream *stream;
	gchar *uid, *resp, *tempuid;
	GData *data;
	gint k = 0, ct;

	if (store->server_level >= IMAP_LEVEL_IMAP4REV1) {
		if (store->headers == IMAP_FETCH_ALL_HEADERS)
			header_spec = g_string_new ("HEADER");
		else {
			gchar *temp;
			header_spec = g_string_new ("HEADER.FIELDS (");
			header_spec = g_string_append (header_spec, CAMEL_MESSAGE_INFO_HEADERS);
			if (store->headers == IMAP_FETCH_MAILING_LIST_HEADERS)
				header_spec = g_string_append (header_spec, MAILING_LIST_HEADERS);
			if (store->custom_headers)
				header_spec = g_string_append (header_spec, store->custom_headers);

			temp = g_strdup(header_spec->str);
			temp = g_strstrip (temp);
			header_spec = g_string_new (temp);
			g_free (temp);
			header_spec = g_string_append (header_spec, ")");
		}
	} else
		header_spec = g_string_new ("0");

	d(printf("Header is : %s", header_spec->str));

	/* Figure out if any of the new messages are already cached (which
	 * may be the case if we're re-syncing after disconnected operation).
	 * If so, get their UIDs, FLAGS, and SIZEs. If not, get all that
	 * and ask for the headers too at the same time.
	 */
	seq = camel_folder_summary_count (folder->summary);
	first = seq + 1;
	if (seq > 0) {
		tempuid = camel_folder_summary_uid_from_index (folder->summary, seq -1 );

		if (tempuid) {
			uidval = strtoul(tempuid, NULL, 10);
			g_free (tempuid);
		} else
			uidval = 0;
	} else
		uidval = 0;

	got = 0;
	if (!camel_imap_command_start (store, folder, ex,
				       "UID FETCH %d:* (FLAGS RFC822.SIZE INTERNALDATE BODYSTRUCTURE BODY.PEEK[%s])",
				       uidval + 1, header_spec->str)) {
		g_string_free (header_spec, TRUE);
		return;
	}
	camel_operation_start (NULL, _("Fetching summary information for new messages in %s"), folder->name);

	/* Parse the responses. We can't add a message to the summary
	 * until we've gotten its headers, and there's no guarantee
	 * the server will send the responses in a useful order...
	 */
	fetch_data = g_ptr_array_new ();
	messages = g_ptr_array_new ();
	ct = exists - seq;
	while ((type = camel_imap_command_response (store, &resp, ex)) ==
	       CAMEL_IMAP_RESPONSE_UNTAGGED && !camel_application_is_exiting) {
		data = parse_fetch_response (imap_folder, resp);
		g_free (resp);
		k++;
		if (!data)
			continue;

		seq = GPOINTER_TO_INT (g_datalist_get_data (&data, "SEQUENCE"));
		if (seq < first) {
			g_datalist_clear (&data);
			continue;
		}

		if (g_datalist_get_data (&data, "FLAGS"))
			got += IMAP_PRETEND_SIZEOF_FLAGS;
		if (g_datalist_get_data (&data, "RFC822.SIZE"))
			got += IMAP_PRETEND_SIZEOF_SIZE;
		stream = g_datalist_get_data (&data, "BODY_PART_STREAM");
		if (stream) {
			got += IMAP_PRETEND_SIZEOF_HEADERS;

			/* Use the stream now so we don't tie up many
			 * many fds if we're fetching many many messages.
			 */
			add_message_from_data (folder, messages, first, data);
			g_datalist_set_data (&data, "BODY_PART_STREAM", NULL);
		}

		camel_operation_progress (NULL, k * 100 / ct);
		g_ptr_array_add (fetch_data, data);
	}
	camel_operation_end (NULL);

	if (type == CAMEL_IMAP_RESPONSE_ERROR || camel_application_is_exiting) {
		if (type != CAMEL_IMAP_RESPONSE_ERROR && type != CAMEL_IMAP_RESPONSE_TAGGED)
			CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

		goto lose;
	}

	/* Free the final tagged response */
	g_free (resp);

	/* Figure out which headers we still need to fetch. */
	needheaders = g_ptr_array_new ();
	size = got = 0;
	for (i = 0; i < fetch_data->len; i++) {
		data = fetch_data->pdata[i];
		if (g_datalist_get_data (&data, "BODY_PART_LEN"))
			continue;

		uid = g_datalist_get_data (&data, "UID");
		if (uid) {
			g_ptr_array_add (needheaders, uid);
			size += IMAP_PRETEND_SIZEOF_HEADERS;
		}
	}

	/* And fetch them */
	if (needheaders->len) {
		gchar *uidset;
		gint uid = 0;

		qsort (needheaders->pdata, needheaders->len,
		       sizeof (gpointer), uid_compar);

		camel_operation_start (NULL, _("Fetching summary information for new messages in %s"), folder->name);

		while (uid < needheaders->len && !camel_application_is_exiting) {
			uidset = imap_uid_array_to_set (folder->summary, needheaders, uid, UID_SET_LIMIT, &uid);
			if (!camel_imap_command_start (store, folder, ex,
						       "UID FETCH %s BODYSTRUCTURE BODY.PEEK[%s]",
						       uidset, header_spec->str)) {
				g_ptr_array_free (needheaders, TRUE);
				camel_operation_end (NULL);
				g_free (uidset);
				g_string_free (header_spec, TRUE);
				goto lose;
			}
			g_free (uidset);

			while ((type = camel_imap_command_response (store, &resp, ex))
			       == CAMEL_IMAP_RESPONSE_UNTAGGED && !camel_application_is_exiting) {
				data = parse_fetch_response (imap_folder, resp);
				g_free (resp);
				if (!data)
					continue;

				stream = g_datalist_get_data (&data, "BODY_PART_STREAM");
				if (stream) {
					add_message_from_data (folder, messages, first, data);
					got += IMAP_PRETEND_SIZEOF_HEADERS;
					camel_operation_progress (NULL, got * 100 / size);
				}
				g_datalist_clear (&data);
			}

			if (type == CAMEL_IMAP_RESPONSE_ERROR || camel_application_is_exiting) {
				g_ptr_array_free (needheaders, TRUE);
				camel_operation_end (NULL);

				if (type != CAMEL_IMAP_RESPONSE_ERROR && type != CAMEL_IMAP_RESPONSE_TAGGED)
					CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

				goto lose;
			}
		}
		g_string_free (header_spec, TRUE);
		g_ptr_array_free (needheaders, TRUE);
		camel_operation_end (NULL);
	}

	/* Now finish up summary entries (fix UIDs, set flags and size) */
	for (i = 0; i < fetch_data->len; i++) {
		struct _junk_data jdata;
		data = fetch_data->pdata[i];

		seq = GPOINTER_TO_INT (g_datalist_get_data (&data, "SEQUENCE"));
		if (seq >= first + messages->len) {
			g_datalist_clear (&data);
			continue;
		}

		mi = messages->pdata[seq - first];
		if (mi == NULL) {
			CamelMessageInfo *pmi = NULL;
			gint j;

			/* This is a kludge around a bug in Exchange
			 * 5.5 that sometimes claims multiple messages
			 * have the same UID. See bug #17694 for
			 * details. The "solution" is to create a fake
			 * message-info with the same details as the
			 * previously valid message. Yes, the user
			 * will have a clone in his/her message-list,
			 * but at least we don't crash.
			 */

			/* find the previous valid message info */
			for (j = seq - first - 1; j >= 0; j--) {
				pmi = messages->pdata[j];
				if (pmi != NULL)
					break;
			}

			if (pmi == NULL) {
				continue;
			}

			mi = (CamelImapMessageInfo *)camel_message_info_clone(pmi);
		}

		uid = g_datalist_get_data (&data, "UID");
		if (uid)
			mi->info.uid = camel_pstring_strdup (uid);
		flags = GPOINTER_TO_INT (g_datalist_get_data (&data, "FLAGS"));
		if (flags) {
			gchar *custom_flags = NULL;

			((CamelImapMessageInfo *)mi)->server_flags = flags;
			/* "or" them in with the existing flags that may
			 * have been set by summary_info_new_from_message.
			 */
			mi->info.flags |= flags;

			custom_flags = g_datalist_get_data (&data, "CUSTOM.FLAGS");
			if (custom_flags)
				fillup_custom_flags ((CamelMessageInfo *)mi, custom_flags);
		}
		size = GPOINTER_TO_INT (g_datalist_get_data (&data, "RFC822.SIZE"));
		if (size)
			mi->info.size = size;

		/* Just do this to build the junk required headers to be built*/
		jdata.data = data;
		jdata.mi = (CamelMessageInfoBase *) mi;
		g_hash_table_foreach ((GHashTable *)camel_session_get_junk_headers(((CamelService *) store)->session), (GHFunc) construct_junk_headers, &jdata);
		g_datalist_clear (&data);
	}
	g_ptr_array_free (fetch_data, TRUE);

	if (camel_application_is_exiting) {
		/* it will hopefully update summary next time */
		fetch_data = NULL;
		goto lose;
	}

	/* And add the entries to the summary, etc. */
	for (i = 0; i < messages->len; i++) {
		mi = messages->pdata[i];
		if (!mi) {
			g_warning ("No information for message %d", i + first);
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Incomplete server response: no information provided for message %d"),
					      i + first);
			break;
		}
		uid = (gchar *)camel_message_info_uid(mi);
		if (uid[0] == 0) {
			g_warning("Server provided no uid: message %d", i + first);
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
					      _("Incomplete server response: no UID provided for message %d"),
					      i + first);
			break;
		}

		/* FIXME: If it enters if (info) it will always match the exception. So stupid */
		/* FIXME[disk-summary] Use a db query to see if the DB exists */
/*		info = (CamelImapMessageInfo *)camel_folder_summary_uid(folder->summary, uid); */
/*		if (info) { */
/*			for (seq = 0; seq < camel_folder_summary_count (folder->summary); seq++) { */
/*				if (folder->summary->messages->pdata[seq] == info) */
/*					break; */
/*			} */

/*			g_warning("Message already present? %s", camel_message_info_uid(mi)); */
/*			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, */
/*					      _("Unexpected server response: Identical UIDs provided for messages %d and %d"), */
/*					      seq + 1, i + first); */

/*			camel_message_info_free(&info->info); */
/*			break; */
/*		}  */

		((CamelMessageInfoBase *)mi)->dirty = TRUE;
		if (((CamelMessageInfoBase *)mi)->summary)
			camel_folder_summary_touch (((CamelMessageInfoBase *)mi)->summary);
		camel_folder_summary_add (folder->summary, (CamelMessageInfo *)mi);
		update_summary (folder->summary, (CamelMessageInfoBase *)mi);
		camel_folder_change_info_add_uid (changes, camel_message_info_uid (mi));

		/* Report all new messages as recent, even without that flag, thus new
		   messages will be filtered even after saw by other software earlier.
		   Only skip those which we added ourself, like after drag&drop to this folder. */
		if (!imap_folder_uid_in_ignore_recent (imap_folder, camel_message_info_uid (mi))
		    && ((mi->info.flags & CAMEL_IMAP_MESSAGE_RECENT) != 0 || getenv ("FILTER_RECENT") == NULL))
			camel_folder_change_info_recent_uid (changes, camel_message_info_uid (mi));

	}

	g_ptr_array_free (messages, TRUE);

	if (imap_folder->priv->ignore_recent) {
		g_hash_table_unref (imap_folder->priv->ignore_recent);
		imap_folder->priv->ignore_recent = NULL;
	}

	return;

 lose:
	if (fetch_data) {
		for (i = 0; i < fetch_data->len; i++) {
			data = fetch_data->pdata[i];
			g_datalist_clear (&data);
		}
		g_ptr_array_free (fetch_data, TRUE);
	}
	if (messages) {
		for (i = 0; i < messages->len; i++) {
			if (messages->pdata[i])
				camel_message_info_free(messages->pdata[i]);
		}
		g_ptr_array_free (messages, TRUE);
	}

	if (imap_folder->priv->ignore_recent) {
		g_hash_table_unref (imap_folder->priv->ignore_recent);
		imap_folder->priv->ignore_recent = NULL;
	}
}

/* Called with the store's connect_lock locked */
void
camel_imap_folder_changed (CamelFolder *folder, gint exists,
			   GArray *expunged, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelFolderChangeInfo *changes;
	gint len;
	gchar *uid;

	changes = camel_folder_change_info_new ();
	if (expunged) {
		gint i, id;
		GSList *deleted = NULL;

		for (i = 0; i < expunged->len; i++) {
			id = g_array_index (expunged, int, i);
			uid = camel_folder_summary_uid_from_index (folder->summary, id - 1);
			if (uid == NULL) {
				/* FIXME: danw: does this mean that the summary is corrupt? */
				/* I guess a message that we never retrieved got expunged? */
				continue;
			}

			deleted = g_slist_prepend(deleted, uid);
			camel_folder_change_info_remove_uid (changes, uid);
			CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);
			camel_imap_message_cache_remove (imap_folder->cache, uid);
			CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);
			camel_folder_summary_remove_index_fast (folder->summary, id-1);
		}

		/* Delete all in one transaction */
		camel_db_delete_uids (folder->parent_store->cdb_w, folder->full_name, deleted, ex);
		g_slist_foreach (deleted, (GFunc) g_free, NULL);
		g_slist_free (deleted);
	}

	len = camel_folder_summary_count (folder->summary);
	if (exists > len && !camel_application_is_exiting)
		imap_update_summary (folder, exists, changes, ex);

	camel_folder_summary_save_to_db (folder->summary, ex);
	if (camel_folder_change_info_changed (changes))
		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", changes);

	camel_folder_change_info_free (changes);
}

static void
imap_thaw (CamelFolder *folder)
{
	CamelImapFolder *imap_folder;

	CAMEL_FOLDER_CLASS (offline_folder_class)->thaw (folder);
	if (camel_folder_is_frozen (folder))
		return;

	imap_folder = CAMEL_IMAP_FOLDER (folder);
	if (imap_folder->need_refresh) {
		CamelException ex = CAMEL_EXCEPTION_INITIALISER;

		imap_folder->need_refresh = FALSE;
		imap_refresh_info (folder, &ex);
		camel_exception_clear (&ex);
	}
}

CamelStream *
camel_imap_folder_fetch_data (CamelImapFolder *imap_folder, const gchar *uid,
			      const gchar *section_text, gboolean cache_only,
			      CamelException *ex)
{
	CamelFolder *folder = CAMEL_FOLDER (imap_folder);
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	CamelStream *stream;
	GData *fetch_data;
	gchar *found_uid;
	gint i;

	/* EXPUNGE responses have to modify the cache, which means
	 * they have to grab the cache_lock while holding the
	 * connect_lock.

	 * Because getting the service lock may cause MUCH unecessary
	 * delay when we already have the data locally, we do the
	 * locking separately.  This could cause a race
	 * getting the same data from the cache, but that is only
	 * an inefficiency, and bad luck.
	 */
	CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);
	stream = camel_imap_message_cache_get (imap_folder->cache, uid, section_text, ex);
	if (!stream && (!strcmp (section_text, "HEADER") || !strcmp (section_text, "0"))) {
		camel_exception_clear (ex);
		stream = camel_imap_message_cache_get (imap_folder->cache, uid, "", ex);
	}
	CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);

	if (stream || cache_only)
		return stream;

	camel_exception_clear(ex);

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);
	CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);

	if (!camel_imap_store_connected(store, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				     _("This message is not currently available"));
		CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return NULL;
	}

	camel_exception_clear (ex);
	if (store->server_level < IMAP_LEVEL_IMAP4REV1 && !*section_text) {
		response = camel_imap_command (store, folder, ex,
					       "UID FETCH %s RFC822.PEEK",
					       uid);
	} else {
		response = camel_imap_command (store, folder, ex,
					       "UID FETCH %s BODY.PEEK[%s]",
					       uid, section_text);
	}
	/* We won't need the connect_lock again after this. */
	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

	if (!response) {
		CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);
		return NULL;
	}

	for (i = 0; i < response->untagged->len; i++) {
		fetch_data = parse_fetch_response (imap_folder, response->untagged->pdata[i]);
		found_uid = g_datalist_get_data (&fetch_data, "UID");
		stream = g_datalist_get_data (&fetch_data, "BODY_PART_STREAM");
		if (found_uid && stream && !strcmp (uid, found_uid))
			break;

		g_datalist_clear (&fetch_data);
		stream = NULL;
	}
	camel_imap_response_free (store, response);
	CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);
	if (!stream) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not find message body in FETCH response."));
	} else {
		camel_object_ref (CAMEL_OBJECT (stream));
		g_datalist_clear (&fetch_data);
	}

	return stream;
}

static GData *
parse_fetch_response (CamelImapFolder *imap_folder, gchar *response)
{
	GData *data = NULL;
	gchar *start, *part_spec = NULL, *body = NULL, *uid = NULL, *idate = NULL;
	gboolean cache_header = TRUE, header = FALSE;
	gsize body_len = 0;

	if (*response != '(') {
		glong seq;

		if (*response != '*' || *(response + 1) != ' ')
			return NULL;
		seq = strtoul (response + 2, &response, 10);
		if (seq == 0)
			return NULL;
		if (g_ascii_strncasecmp (response, " FETCH (", 8) != 0)
			return NULL;
		response += 7;

		g_datalist_set_data (&data, "SEQUENCE", GINT_TO_POINTER (seq));
	}

	do {
		/* Skip the initial '(' or the ' ' between elements */
		response++;

		if (!g_ascii_strncasecmp (response, "FLAGS ", 6)) {
			guint32 flags;
			gchar *custom_flags = NULL;

			response += 6;

			if (imap_parse_flag_list (&response, &flags, &custom_flags)) {
				g_datalist_set_data (&data, "FLAGS", GUINT_TO_POINTER (flags));

				if (custom_flags)
					g_datalist_set_data_full (&data, "CUSTOM.FLAGS", custom_flags, g_free);
			}
		} else if (!g_ascii_strncasecmp (response, "RFC822.SIZE ", 12)) {
			gulong size;

			response += 12;
			size = strtoul (response, &response, 10);
			g_datalist_set_data (&data, "RFC822.SIZE", GUINT_TO_POINTER (size));
		} else if (!g_ascii_strncasecmp (response, "BODY[", 5) ||
			   !g_ascii_strncasecmp (response, "RFC822 ", 7)) {
			gchar *p;

			if (*response == 'B') {
				response += 5;

				/* HEADER], HEADER.FIELDS (...)], or 0] */
				if (!g_ascii_strncasecmp (response, "HEADER", 6)) {
					header = TRUE;
					if (!g_ascii_strncasecmp (response + 6, ".FIELDS", 7))
						cache_header = FALSE;
				} else if (!g_ascii_strncasecmp (response, "0]", 2))
					header = TRUE;

				p = strchr (response, ']');
				if (!p || *(p + 1) != ' ')
					break;

				if (cache_header)
					part_spec = g_strndup (response, p - response);
				else
					part_spec = g_strdup ("HEADER.FIELDS");

				response = p + 2;
			} else {
				part_spec = g_strdup ("");
				response += 7;

				if (!g_ascii_strncasecmp (response, "HEADER", 6))
					header = TRUE;
			}

			body = imap_parse_nstring ((const gchar **) &response, &body_len);
			if (!response) {
				g_free (part_spec);
				break;
			}

			if (!body)
				body = g_strdup ("");
			g_datalist_set_data_full (&data, "BODY_PART_SPEC", part_spec, g_free);
			g_datalist_set_data_full (&data, "BODY_PART_DATA", body, g_free);
			g_datalist_set_data (&data, "BODY_PART_LEN", GINT_TO_POINTER (body_len));
		} else if (!g_ascii_strncasecmp (response, "BODY ", 5) ||
			   !g_ascii_strncasecmp (response, "BODYSTRUCTURE ", 14)) {
			response = strchr (response, ' ') + 1;
			start = response;
			imap_skip_list ((const gchar **) &response);
			if (response && (response != start)) {
				/* To handle IMAP Server brokenness, Returning empty body, etc. See #355640 */
				g_datalist_set_data_full (&data, "BODY", g_strndup (start, response - start), g_free);
			}
		} else if (!g_ascii_strncasecmp (response, "UID ", 4)) {
			gint len;

			len = strcspn (response + 4, " )");
			uid = g_strndup (response + 4, len);
			g_datalist_set_data_full (&data, "UID", uid, g_free);
			response += 4 + len;
		} else if (!g_ascii_strncasecmp (response, "INTERNALDATE ", 13)) {
			gint len;

			response += 13;
			if (*response == '"') {
				response++;
				len = strcspn (response, "\"");
				idate = g_strndup (response, len);
				g_datalist_set_data_full (&data, "INTERNALDATE", idate, g_free);
				response += len + 1;
			}
		} else {
			g_warning ("Unexpected FETCH response from server: (%s", response);
			break;
		}
	} while (response && *response != ')');

	if (!response || *response != ')') {
		g_datalist_clear (&data);
		return NULL;
	}

	if (uid && body) {
		CamelStream *stream;

		if (header && !cache_header) {
			stream = camel_stream_mem_new_with_buffer (body, body_len);
		} else {
			CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);
			stream = camel_imap_message_cache_insert (imap_folder->cache,
								  uid, part_spec,
								  body, body_len, NULL);
			CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);
			if (stream == NULL)
				stream = camel_stream_mem_new_with_buffer (body, body_len);
		}

		if (stream)
			g_datalist_set_data_full (&data, "BODY_PART_STREAM", stream,
						  (GDestroyNotify) camel_object_unref);
	}

	return data;
}

/* it uses connect_lock, thus be sure it doesn't run in main thread */
static CamelFolderQuotaInfo *
imap_get_quota_info (CamelFolder *folder)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	CamelFolderQuotaInfo *res = NULL, *last = NULL;

	if (CAMEL_OFFLINE_STORE (imap_store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
		return NULL;

	CAMEL_SERVICE_REC_LOCK (imap_store, connect_lock);

	if (!camel_imap_store_connected (imap_store, NULL))
		goto done;

	if (imap_store->capabilities & IMAP_CAPABILITY_QUOTA) {
		const gchar *full_name = camel_folder_get_full_name (folder);
		CamelImapStoreNamespace *ns = camel_imap_store_summary_namespace_find_full (imap_store->summary, full_name);
		gchar *folder_name = camel_imap_store_summary_path_to_full (imap_store->summary, full_name, ns ? ns->sep : '/');

		response = camel_imap_command (imap_store, NULL, NULL, "GETQUOTAROOT \"%s\"", folder_name);

		if (response) {
			gint i;

			for (i = 0; i < response->untagged->len; i++) {
				const gchar *resp = response->untagged->pdata[i];

				if (resp && g_str_has_prefix (resp, "* QUOTA ")) {
					gboolean skipped = TRUE;
					gsize sz;
					gchar *astr;

					resp = resp + 8;
					astr = imap_parse_astring (&resp, &sz);
					g_free (astr);

					while (resp && *resp && *resp != '(')
						resp++;

					if (resp && *resp == '(') {
						gchar *name;
						const gchar *used = NULL, *total = NULL;

						resp++;
						name = imap_parse_astring (&resp, &sz);

						if (resp)
							used = imap_next_word (resp);
						if (used)
							total = imap_next_word (used);

						while (resp && *resp && *resp != ')')
							resp++;

						if (resp && *resp == ')' && used && total) {
							guint64 u, t;

							u = strtoull (used, NULL, 10);
							t = strtoull (total, NULL, 10);

							if (t > 0) {
								CamelFolderQuotaInfo *info = camel_folder_quota_info_new (name, u, t);

								if (last)
									last->next = info;
								else
									res = info;

								last = info;
								skipped = FALSE;
							}
						}

						g_free (name);
					}

					if (skipped)
						g_debug ("Unexpected quota response '%s'; skipping it...", (const gchar *)response->untagged->pdata[i]);
				}
			}
			camel_imap_response_free (imap_store, response);
		}

		g_free (folder_name);
	}
done:
	CAMEL_SERVICE_REC_UNLOCK (imap_store, connect_lock);
	return res;
}

/**
 * Scan for messages that are local and return the rest.
 */
static GPtrArray *
imap_get_uncached_uids (CamelFolder *folder, GPtrArray * uids, CamelException *ex)
{
	GPtrArray *result;
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);

	CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);
	result = camel_imap_message_cache_filter_cached (imap_folder->cache, uids, ex);
	CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);
	return result;
}


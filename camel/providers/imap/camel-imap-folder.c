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

#include <camel/camel.h>

#include "camel-imap-command.h"
#include "camel-imap-folder.h"
#include "camel-imap-journal.h"
#include "camel-imap-message-cache.h"
#include "camel-imap-private.h"
#include "camel-imap-search.h"
#include "camel-imap-settings.h"
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

#define CAMEL_IMAP_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAP_FOLDER, CamelImapFolderPrivate))

/* The custom property ID is a CamelArg artifact.
 * It still identifies the property in state files. */
enum {
	PROP_0,
	PROP_CHECK_FOLDER = 0x2500,
	PROP_APPLY_FILTERS
};

extern gint camel_application_is_exiting;

static gboolean imap_rescan (CamelFolder *folder, gint exists, GCancellable *cancellable, GError **error);
static gboolean imap_refresh_info_sync (CamelFolder *folder, GCancellable *cancellable, GError **error);
static gboolean imap_sync_offline (CamelFolder *folder, GError **error);
static gboolean imap_synchronize_sync (CamelFolder *folder, gboolean expunge, GCancellable *cancellable, GError **error);
static gboolean imap_expunge_uids_online (CamelFolder *folder, GPtrArray *uids, GCancellable *cancellable, GError **error);
static gboolean imap_expunge_uids_offline (CamelFolder *folder, GPtrArray *uids, GCancellable *cancellable, GError **error);
static gboolean imap_expunge_sync (CamelFolder *folder, GCancellable *cancellable, GError **error);
/*static void imap_cache_message (CamelDiscoFolder *disco_folder, const gchar *uid, GError **error);*/
static void imap_rename (CamelFolder *folder, const gchar *new);
static GPtrArray * imap_get_uncached_uids (CamelFolder *folder, GPtrArray * uids, GError **error);
static gchar * imap_get_filename (CamelFolder *folder, const gchar *uid, GError **error);

/* message manipulation */
static CamelMimeMessage *imap_get_message_cached (CamelFolder *folder, const gchar *message_uid, GCancellable *cancellable);
static CamelMimeMessage *imap_get_message_sync (CamelFolder *folder, const gchar *uid, GCancellable *cancellable,
					   GError **error);
static gboolean imap_synchronize_message_sync (CamelFolder *folder, const gchar *uid, GCancellable *cancellable,
			       GError **error);
static gboolean imap_append_online (CamelFolder *folder, CamelMimeMessage *message,
				CamelMessageInfo *info, gchar **appended_uid,
				GCancellable *cancellable,
				GError **error);
static gboolean imap_append_offline (CamelFolder *folder, CamelMimeMessage *message,
				 CamelMessageInfo *info, gchar **appended_uid,
				 GError **error);

static gboolean	imap_transfer_online		(CamelFolder *source,
						 GPtrArray *uids,
						 CamelFolder *dest,
						 gboolean delete_originals,
						 GPtrArray **transferred_uids,
						 GCancellable *cancellable,
						 GError **error);
static gboolean	imap_transfer_offline		(CamelFolder *source,
						 GPtrArray *uids,
						 CamelFolder *dest,
						 gboolean delete_originals,
						 GPtrArray **transferred_uids,
						 GCancellable *cancellable,
						 GError **error);

/* searching */
static GPtrArray *imap_search_by_expression (CamelFolder *folder, const gchar *expression, GCancellable *cancellable, GError **error);
static guint32 imap_count_by_expression (CamelFolder *folder, const gchar *expression, GCancellable *cancellable, GError **error);
static GPtrArray *imap_search_by_uids	    (CamelFolder *folder, const gchar *expression, GPtrArray *uids, GCancellable *cancellable, GError **error);
static void       imap_search_free          (CamelFolder *folder, GPtrArray *uids);

static void imap_thaw (CamelFolder *folder);
static CamelFolderQuotaInfo *
		imap_get_quota_info_sync	(CamelFolder *folder,
						 GCancellable *cancellable,
						 GError **error);

static GData *parse_fetch_response (CamelImapFolder *imap_folder, gchar *msg_att);

/* internal helpers */
static CamelImapMessageInfo * imap_folder_summary_uid_or_error (
	CamelFolderSummary *summary,
	const gchar * uid,
	GError **error);

static gboolean	imap_transfer_messages		(CamelFolder *source,
						 GPtrArray *uids,
						 CamelFolder *dest,
						 gboolean delete_originals,
						 GPtrArray **transferred_uids,
						 gboolean can_call_sync,
						 GCancellable *cancellable,
						 GError **error);

static gboolean
imap_folder_get_apply_filters (CamelImapFolder *folder)
{
	g_return_val_if_fail (folder != NULL, FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAP_FOLDER (folder), FALSE);

	return folder->priv->apply_filters;
}

static void
imap_folder_set_apply_filters (CamelImapFolder *folder,
                               gboolean apply_filters)
{
	g_return_if_fail (folder != NULL);
	g_return_if_fail (CAMEL_IS_IMAP_FOLDER (folder));

	if (folder->priv->apply_filters == apply_filters)
		return;

	folder->priv->apply_filters = apply_filters;

	g_object_notify (G_OBJECT (folder), "apply-filters");
}

#ifdef G_OS_WIN32
/* The strtok() in Microsoft's C library is MT-safe (but still uses
 * only one buffer pointer per thread, but for the use of strtok_r()
 * here that's enough).
 */
#define strtok_r(s,sep,lasts) (*(lasts)=strtok((s),(sep)))
#endif

G_DEFINE_TYPE (CamelImapFolder, camel_imap_folder, CAMEL_TYPE_OFFLINE_FOLDER)

static void
imap_folder_set_property (GObject *object,
                          guint property_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CHECK_FOLDER:
			camel_imap_folder_set_check_folder (
				CAMEL_IMAP_FOLDER (object),
				g_value_get_boolean (value));
			return;

		case PROP_APPLY_FILTERS:
			imap_folder_set_apply_filters (
				CAMEL_IMAP_FOLDER (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imap_folder_get_property (GObject *object,
                          guint property_id,
                          GValue *value,
                          GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CHECK_FOLDER:
			g_value_set_boolean (
				value, camel_imap_folder_get_check_folder (
				CAMEL_IMAP_FOLDER (object)));
			return;

		case PROP_APPLY_FILTERS:
			g_value_set_boolean (
				value, imap_folder_get_apply_filters (
				CAMEL_IMAP_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imap_folder_dispose (GObject *object)
{
	CamelImapFolder *imap_folder;
	CamelStore *parent_store;

	imap_folder = CAMEL_IMAP_FOLDER (object);

	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (imap_folder));
	if (parent_store) {
		camel_store_summary_disconnect_folder_summary (
			(CamelStoreSummary *) ((CamelImapStore *) parent_store)->summary,
			CAMEL_FOLDER (imap_folder)->summary);
	}

	if (imap_folder->search != NULL) {
		g_object_unref (imap_folder->search);
		imap_folder->search = NULL;
	}

	if (imap_folder->cache != NULL) {
		g_object_unref (imap_folder->cache);
		imap_folder->cache = NULL;
	}

	if (imap_folder->priv->ignore_recent != NULL) {
		g_hash_table_unref (imap_folder->priv->ignore_recent);
		imap_folder->priv->ignore_recent = NULL;
	}

	if (imap_folder->journal != NULL) {
		camel_offline_journal_write (imap_folder->journal, NULL);
		g_object_unref (imap_folder->journal);
		imap_folder->journal = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imap_folder_parent_class)->dispose (object);
}

static void
imap_folder_finalize (GObject *object)
{
	CamelImapFolder *imap_folder;

	imap_folder = CAMEL_IMAP_FOLDER (object);

	g_static_mutex_free (&imap_folder->priv->search_lock);
	g_static_rec_mutex_free (&imap_folder->priv->cache_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imap_folder_parent_class)->finalize (object);
}

static void
imap_folder_constructed (GObject *object)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelService *service;
	CamelFolder *folder;
	CamelStore *parent_store;
	const gchar *full_name;
	gchar *description;
	gchar *host;
	gchar *user;

	folder = CAMEL_FOLDER (object);
	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	service = CAMEL_SERVICE (parent_store);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	description = g_strdup_printf (
		"%s@%s:%s", user, host, full_name);
	camel_folder_set_description (folder, description);
	g_free (description);

	g_free (host);
	g_free (user);
}

static void
camel_imap_folder_class_init (CamelImapFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	g_type_class_add_private (class, sizeof (CamelImapFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imap_folder_set_property;
	object_class->get_property = imap_folder_get_property;
	object_class->dispose = imap_folder_dispose;
	object_class->finalize = imap_folder_finalize;
	object_class->constructed = imap_folder_constructed;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->rename = imap_rename;
	folder_class->search_by_expression = imap_search_by_expression;
	folder_class->count_by_expression = imap_count_by_expression;
	folder_class->search_by_uids = imap_search_by_uids;
	folder_class->search_free = imap_search_free;
	folder_class->thaw = imap_thaw;
	folder_class->get_uncached_uids = imap_get_uncached_uids;
	folder_class->get_filename = imap_get_filename;
	folder_class->append_message_sync = imap_append_online;
	folder_class->expunge_sync = imap_expunge_sync;
	folder_class->get_message_cached = imap_get_message_cached;
	folder_class->get_message_sync = imap_get_message_sync;
	folder_class->get_quota_info_sync = imap_get_quota_info_sync;
	folder_class->refresh_info_sync = imap_refresh_info_sync;
	folder_class->synchronize_sync = imap_synchronize_sync;
	folder_class->synchronize_message_sync = imap_synchronize_message_sync;
	folder_class->transfer_messages_to_sync = imap_transfer_online;

	g_object_class_install_property (
		object_class,
		PROP_CHECK_FOLDER,
		g_param_spec_boolean (
			"check-folder",
			"Check Folder",
			_("Always check for _new mail in this folder"),
			FALSE,
			G_PARAM_READWRITE |
			CAMEL_PARAM_PERSISTENT));

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
}

static void
camel_imap_folder_init (CamelImapFolder *imap_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (imap_folder);

	imap_folder->priv = CAMEL_IMAP_FOLDER_GET_PRIVATE (imap_folder);

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN;

	folder->folder_flags |= CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY;

	g_static_mutex_init (&imap_folder->priv->search_lock);
	g_static_rec_mutex_init (&imap_folder->priv->cache_lock);
	imap_folder->priv->ignore_recent = NULL;

	imap_folder->journal = NULL;
	imap_folder->need_rescan = TRUE;
}

static void
replay_offline_journal (CamelImapStore *imap_store,
                        CamelImapFolder *imap_folder,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelIMAPJournal *imap_journal;

	g_return_if_fail (imap_store != NULL);
	g_return_if_fail (imap_folder != NULL);
	g_return_if_fail (imap_folder->journal != NULL);

	imap_journal = CAMEL_IMAP_JOURNAL (imap_folder->journal);
	g_return_if_fail (imap_journal != NULL);

	/* do not replay when still in offline */
	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (imap_store)) || !camel_imap_store_connected (imap_store, error))
		return;

	/* Check if the replay is already in progress as imap_sync would be called while expunge resync */
	if (!imap_journal->rp_in_progress) {
		imap_journal->rp_in_progress++;

		camel_offline_journal_replay (
			imap_folder->journal, cancellable, error);
		camel_imap_journal_close_folders (imap_journal);
		camel_offline_journal_write (imap_folder->journal, error);

		imap_journal->rp_in_progress--;
		g_return_if_fail (imap_journal->rp_in_progress >= 0);
	}
}

CamelFolder *
camel_imap_folder_new (CamelStore *parent,
                       const gchar *folder_name,
                       const gchar *folder_dir,
                       GError **error)
{
	CamelFolder *folder;
	CamelImapFolder *imap_folder;
	const gchar *short_name;
	gchar *state_file, *path;
	CamelService *service;
	CamelSettings *settings;
	gboolean filter_all;
	gboolean filter_inbox;
	gboolean filter_junk;
	gboolean filter_junk_inbox;

	if (g_mkdir_with_parents (folder_dir, S_IRWXU) != 0) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not create directory %s: %s"),
			folder_dir, g_strerror (errno));
		return NULL;
	}

	short_name = strrchr (folder_name, '/');
	if (short_name)
		short_name++;
	else
		short_name = folder_name;
	folder = g_object_new (
		CAMEL_TYPE_IMAP_FOLDER,
		"full-name", folder_name,
		"display-name", short_name,
		"parent-store", parent, NULL);

	folder->summary = camel_imap_summary_new (folder);
	if (!folder->summary) {
		g_object_unref (folder);
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not load summary for %s"), folder_name);
		return NULL;
	}

	imap_folder = CAMEL_IMAP_FOLDER (folder);
	path = g_build_filename (folder_dir, "journal", NULL);
	imap_folder->journal = camel_imap_journal_new (imap_folder, path);
	g_free (path);

	/* set/load persistent state */
	state_file = g_build_filename (folder_dir, "cmeta", NULL);
	camel_object_set_state_filename (CAMEL_OBJECT (folder), state_file);
	g_free (state_file);
	camel_object_state_read (CAMEL_OBJECT (folder));

	imap_folder->cache = camel_imap_message_cache_new (folder_dir, folder->summary, error);
	if (!imap_folder->cache) {
		g_object_unref (folder);
		return NULL;
	}

	service = CAMEL_SERVICE (parent);
	settings = camel_service_ref_settings (service);

	g_object_get (
		settings,
		"filter-all", &filter_all,
		"filter-inbox", &filter_inbox,
		"filter-junk", &filter_junk,
		"filter-junk-inbox", &filter_junk_inbox,
		NULL);

	if (g_ascii_strcasecmp (folder_name, "INBOX") == 0) {
		if (filter_inbox || filter_all)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
		if (filter_junk)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
		if (filter_junk_inbox)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
	} else {
		gboolean folder_is_trash, folder_is_junk;
		gchar *junk_path;
		gchar *trash_path;

		junk_path = camel_imap_settings_dup_real_junk_path (
			CAMEL_IMAP_SETTINGS (settings));

		/* So we can safely compare strings. */
		if (junk_path == NULL)
			junk_path = g_strdup ("");

		trash_path = camel_imap_settings_dup_real_trash_path (
			CAMEL_IMAP_SETTINGS (settings));

		/* So we can safely compare strings. */
		if (trash_path == NULL)
			trash_path = g_strdup ("");

		if (filter_junk && !filter_junk_inbox)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;

		folder_is_trash =
			(parent->flags & CAMEL_STORE_VTRASH) == 0 &&
			g_ascii_strcasecmp (trash_path, folder_name) == 0;

		if (folder_is_trash)
			folder->folder_flags |= CAMEL_FOLDER_IS_TRASH;

		folder_is_junk =
			(parent->flags & CAMEL_STORE_VJUNK) == 0 &&
			g_ascii_strcasecmp (junk_path, folder_name) == 0;

		if (folder_is_junk)
			folder->folder_flags |= CAMEL_FOLDER_IS_JUNK;

		if (filter_all || imap_folder_get_apply_filters (imap_folder))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

		g_free (junk_path);
		g_free (trash_path);
	}

	g_object_unref (settings);

	imap_folder->search = camel_imap_search_new (folder_dir);

	camel_store_summary_connect_folder_summary (
		(CamelStoreSummary *) ((CamelImapStore *) parent)->summary,
		folder_name, folder->summary);

	return folder;
}

gboolean
camel_imap_folder_get_check_folder (CamelImapFolder *imap_folder)
{
	g_return_val_if_fail (CAMEL_IS_IMAP_FOLDER (imap_folder), FALSE);

	return imap_folder->priv->check_folder;
}

void
camel_imap_folder_set_check_folder (CamelImapFolder *imap_folder,
                                    gboolean check_folder)
{
	CamelFolder *folder;
	CamelStore *parent_store;
	const gchar *full_name;

	g_return_if_fail (CAMEL_IS_IMAP_FOLDER (imap_folder));

	if (imap_folder->priv->check_folder == check_folder)
		return;

	imap_folder->priv->check_folder = check_folder;

	folder = CAMEL_FOLDER (imap_folder);
	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	/* Update the summary so the value is restored
	 * correctly the next time the folder is loaded. */
	if (CAMEL_IS_IMAP_STORE (parent_store)) {
		CamelImapStore *imap_store;
		CamelStoreSummary *summary;
		CamelStoreInfo *si;

		imap_store = CAMEL_IMAP_STORE (parent_store);
		summary = CAMEL_STORE_SUMMARY (imap_store->summary);

		si = camel_store_summary_path (summary, full_name);
		if (si != NULL) {
			guint32 old_flags = si->flags;

			si->flags &= ~CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW;
			si->flags |= check_folder ? CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW : 0;

			if (si->flags != old_flags) {
				camel_store_summary_touch (summary);
				camel_store_summary_save (summary);
			}

			camel_store_summary_info_free (summary, si);
		}
	}

	g_object_notify (G_OBJECT (imap_folder), "check-folder");
}

/* Called with the store's connect_lock locked */
gboolean
camel_imap_folder_selected (CamelFolder *folder,
                            CamelImapResponse *response,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapSummary *imap_summary = CAMEL_IMAP_SUMMARY (folder->summary);
	gulong exists = 0, validity = 0, val, uid;
	CamelMessageFlags perm_flags = 0;
	GData *fetch_data;
	gint i, count;
	gchar *resp;

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
		} else if (isdigit ((guchar) * resp)) {
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

	if (!imap_summary->validity)
		imap_summary->validity = validity;
	else if (validity != imap_summary->validity) {
		imap_summary->validity = validity;
		camel_folder_summary_clear (folder->summary, NULL);
		CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);
		camel_imap_message_cache_clear (imap_folder->cache);
		CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);
		imap_folder->need_rescan = FALSE;
		return camel_imap_folder_changed (
			folder, exists, NULL, cancellable, error);
	}

	/* If we've lost messages, we have to rescan everything */
	if (exists < count)
		imap_folder->need_rescan = TRUE;
	else if (count != 0 && !imap_folder->need_rescan) {
		CamelStore *parent_store;
		CamelImapStore *store;
		GPtrArray *known_uids;
		const gchar *old_uid;

		parent_store = camel_folder_get_parent_store (folder);
		store = CAMEL_IMAP_STORE (parent_store);

		/* Similarly, if the UID of the highest message we
		 * know about has changed, then that indicates that
		 * messages have been both added and removed, so we
		 * have to rescan to find the removed ones. (We pass
		 * NULL for the folder since we know that this folder
		 * is selected, and we don't want camel_imap_command
		 * to worry about it.)
		 */
		response = camel_imap_command (store, NULL, cancellable, error, "FETCH %d UID", count);
		if (!response)
			return FALSE;
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

		known_uids = camel_folder_summary_get_array (folder->summary);
		camel_folder_sort_uids (folder, known_uids);
		old_uid = NULL;
		if (known_uids && count - 1 >= 0 && count - 1 < known_uids->len)
			old_uid = g_ptr_array_index (known_uids, count - 1);
		if (old_uid) {
			val = strtoul (old_uid, NULL, 10);
			if (uid == 0 || uid != val)
				imap_folder->need_rescan = TRUE;
		}
		camel_folder_summary_free_array (known_uids);
	}

	/* Now rescan if we need to */
	if (imap_folder->need_rescan)
		return imap_rescan (folder, exists, cancellable, error);

	/* If we don't need to rescan completely, but new messages
	 * have been added, find out about them.
	 */
	if (exists > count)
		camel_imap_folder_changed (
			folder, exists, NULL, cancellable, error);

	/* And we're done. */

	return TRUE;
}

static gchar *
imap_get_filename (CamelFolder *folder,
                   const gchar *uid,
                   GError **error)
{
	CamelImapFolder *imap_folder = (CamelImapFolder *) folder;

	return camel_imap_message_cache_get_filename (imap_folder->cache, uid, "", error);
}

static void
imap_rename (CamelFolder *folder,
             const gchar *new)
{
	CamelService *service;
	CamelStore *parent_store;
	CamelImapFolder *imap_folder = (CamelImapFolder *) folder;
	const gchar *user_cache_dir;
	gchar *folder_dir, *state_file;
	gchar *folders;

	parent_store = camel_folder_get_parent_store (folder);

	service = CAMEL_SERVICE (parent_store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	folders = g_build_filename (user_cache_dir, "folders", NULL);
	folder_dir = imap_path_to_physical (folders, new);
	g_free (folders);

	CAMEL_IMAP_FOLDER_REC_LOCK (folder, cache_lock);
	camel_imap_message_cache_set_path (imap_folder->cache, folder_dir);
	CAMEL_IMAP_FOLDER_REC_UNLOCK (folder, cache_lock);

	state_file = g_build_filename (folder_dir, "cmeta", NULL);
	camel_object_set_state_filename (CAMEL_OBJECT (folder), state_file);
	g_free (state_file);

	g_free (folder_dir);

	camel_store_summary_disconnect_folder_summary (
		(CamelStoreSummary *) ((CamelImapStore *) parent_store)->summary,
		folder->summary);

	CAMEL_FOLDER_CLASS (camel_imap_folder_parent_class)->rename (folder, new);

	camel_store_summary_connect_folder_summary (
		(CamelStoreSummary *) ((CamelImapStore *) parent_store)->summary,
		camel_folder_get_full_name (folder), folder->summary);
}

/* called with connect_lock locked */
static gboolean
get_folder_status (CamelFolder *folder,
                   guint32 *total,
                   guint32 *unread,
                   GCancellable *cancellable,
                   GError **error)
{
	CamelStore *parent_store;
	CamelImapStore *imap_store;
	CamelImapResponse *response;
	const gchar *full_name;
	gboolean res = FALSE;

	g_return_val_if_fail (folder != NULL, FALSE);

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	imap_store = CAMEL_IMAP_STORE (parent_store);

	response = camel_imap_command (imap_store, folder, cancellable, error, "STATUS %F (MESSAGES UNSEEN)", full_name);

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

static gboolean
imap_refresh_info_sync (CamelFolder *folder,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelStore *parent_store;
	CamelImapStore *imap_store;
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapResponse *response;
	CamelStoreInfo *si;
	const gchar *full_name;
	gint check_rescan = -1;
	GError *local_error = NULL;

	parent_store = camel_folder_get_parent_store (folder);
	imap_store = CAMEL_IMAP_STORE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (imap_store)))
		return TRUE;

	if (camel_folder_is_frozen (folder)) {
		imap_folder->need_refresh = TRUE;
		return TRUE;
	}

	/* If the folder isn't selected, select it (which will force
	 * a rescan if one is needed).
	 * Also, if this is the INBOX, some servers (cryus) wont tell
	 * us with a NOOP of new messages, so force a reselect which
	 * should do it.  */

	if (camel_application_is_exiting  || !camel_imap_store_connected (imap_store, &local_error))
		goto done;

	/* try to store local changes first, as the summary contains new local messages */
	replay_offline_journal (
		imap_store, imap_folder, cancellable, &local_error);

	full_name = camel_folder_get_full_name (folder);

	if (imap_store->current_folder != folder
	    || g_ascii_strcasecmp (full_name, "INBOX") == 0) {
		response = camel_imap_command (imap_store, folder, cancellable, &local_error, NULL);
		if (response) {
			camel_imap_folder_selected (
				folder, response,
				cancellable, &local_error);
			camel_imap_response_free (imap_store, response);
		}
	} else if (imap_folder->need_rescan) {
		/* Otherwise, if we need a rescan, do it, and if not, just do
		 * a NOOP to give the server a chance to tell us about new
		 * messages.
		 */
		imap_rescan (
			folder, camel_folder_summary_count (
			folder->summary), cancellable, &local_error);
		check_rescan = 0;
	} else {
#if 0
		/* on some servers need to CHECKpoint INBOX to recieve new messages?? */
		/* rfc2060 suggests this, but havent seen a server that requires it */
		if (g_ascii_strcasecmp (full_name, "INBOX") == 0) {
			response = camel_imap_command (imap_store, folder, &local_error, "CHECK");
			camel_imap_response_free (imap_store, response);
		}
#endif
		response = camel_imap_command (imap_store, folder, cancellable, &local_error, "NOOP");
		camel_imap_response_free (imap_store, response);
	}

	si = camel_store_summary_path ((CamelStoreSummary *)((CamelImapStore *) parent_store)->summary, full_name);
	if (si) {
		guint32 unread, total;

		total = camel_folder_summary_count (folder->summary);
		unread = camel_folder_summary_get_unread_count (folder->summary);

		if (si->total != total
		    || si->unread != unread) {
			si->total = total;
			si->unread = unread;
			camel_store_summary_touch ((CamelStoreSummary *)((CamelImapStore *) parent_store)->summary);
			check_rescan = 0;
		}
		camel_store_summary_info_free ((CamelStoreSummary *)((CamelImapStore *) parent_store)->summary, si);
	}

	if (check_rescan && !camel_application_is_exiting && local_error == NULL) {
		if (check_rescan == -1) {
			guint32 total, unread = 0, server_total = 0, server_unread = 0;

			check_rescan = 0;

			/* Check whether there are changes in total/unread messages in the folders
			 * and if so, then rescan whole summary */
			if (get_folder_status (folder, &server_total, &server_unread, cancellable, &local_error)) {

				total = camel_folder_summary_count (folder->summary);
				unread = camel_folder_summary_get_unread_count (folder->summary);

				if (total != server_total || unread != server_unread)
					check_rescan = 1;
			}
		}

		if (check_rescan)
			imap_rescan (
				folder, camel_folder_summary_count (
				folder->summary), cancellable, &local_error);
	}

done:
	camel_folder_summary_save_to_db (folder->summary, NULL);
	camel_store_summary_save ((CamelStoreSummary *)((CamelImapStore *) parent_store)->summary);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static void
fillup_custom_flags (CamelMessageInfo *mi,
                     gchar *custom_flags)
{
	gchar **array_str;
	gint index = 0;

	array_str = g_strsplit (custom_flags, " ", -1);

	while (array_str[index] != NULL) {
		camel_flag_set (&((CamelMessageInfoBase *) mi)->user_flags, array_str[index], TRUE);
		++ index;
	}

	g_strfreev (array_str);
}

/* This will merge custom flags with those in message info. Returns whether was some change. */
static gboolean
merge_custom_flags (CamelMessageInfo *mi,
                    const gchar *custom_flags)
{
	GList *list, *p;
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
	for (i = 0; cflags[i]; i++) {
		gchar *name = cflags[i];

		if (name && *name) {
			g_hash_table_insert (server, name, name);
			list = g_list_prepend (list, name);
		}
	}

	for (flag = camel_message_info_user_flags (mi); flag; flag = flag->next) {
		gchar *name = (gchar *) flag->name;

		if (name && *name)
			list = g_list_prepend (list, name);
	}

	list = g_list_sort (list, (GCompareFunc) strcmp);
	for (p = list; p; p = p->next) {
		if (p->next && strcmp (p->data, p->next->data) == 0) {
			/* This flag is there twice, which means it was on the server and
			 * in our local summary too; thus skip these two elements. */
			p = p->next;
		} else {
			/* If this value came from the server, then add it to our local summary,
			 * otherwise it was in local summary, but isn't on the server, thus remove it. */
			changed = TRUE;
			mi->dirty = TRUE;
			if (mi->summary)
				camel_folder_summary_touch (mi->summary);
			camel_flag_set (&((CamelMessageInfoBase *) mi)->user_flags, p->data, g_hash_table_lookup (server, p->data) != NULL);
			((CamelMessageInfoBase *) mi)->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		}
	}

	g_list_free (list);
	g_hash_table_destroy (server);
	g_strfreev (cflags);

	return changed;
}

/* Called with the store's connect_lock locked */
static gboolean
imap_rescan (CamelFolder *folder,
             gint exists,
             GCancellable *cancellable,
             GError **error)
{
	CamelStore *parent_store;
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapStore *store;
	struct {
		gchar *uid;
		guint32 flags;
		gchar *custom_flags;
	} *new;
	gchar *resp, *uid;
	CamelImapResponseType type;
	gint i, j, seq, summary_got, del = 0;
	guint summary_len;
	CamelMessageInfo *info;
	CamelImapMessageInfo *iinfo;
	GArray *removed;
	gboolean ok;
	CamelFolderChangeInfo *changes = NULL;
	gboolean success;
	GPtrArray *known_uids;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_IMAP_STORE (parent_store);

	if (camel_application_is_exiting)
		return TRUE;

	imap_folder->need_rescan = FALSE;

	known_uids = camel_folder_summary_get_array (folder->summary);
	summary_len = known_uids ? known_uids->len : 0;
	if (summary_len == 0) {
		camel_folder_summary_free_array (known_uids);
		if (exists)
			return camel_imap_folder_changed (
				folder, exists, NULL, cancellable, error);
		return TRUE;
	}

	/* Check UIDs and flags of all messages we already know of. */
	camel_operation_push_message (
		cancellable, _("Scanning for changed messages in %s"),
		camel_folder_get_display_name (folder));

	camel_folder_sort_uids (folder, known_uids);

	uid = g_ptr_array_index (known_uids, summary_len - 1);
	if (!uid) {
		camel_operation_pop_message (cancellable);
		camel_folder_summary_free_array (known_uids);
		return TRUE;
	}

	if (!camel_imap_store_connected (CAMEL_IMAP_STORE (parent_store), error)) {
		camel_operation_pop_message (cancellable);
		camel_folder_summary_free_array (known_uids);
		return FALSE;
	}

	ok = camel_imap_command_start (
		store, folder, cancellable, error,
		"UID FETCH 1:%s (FLAGS)", uid);
	if (!ok) {
		camel_operation_pop_message (cancellable);
		camel_folder_summary_free_array (known_uids);
		return FALSE;
	}

	resp = NULL;
	new = g_malloc0 (summary_len * sizeof (*new));
	summary_got = 0;
	while ((type = camel_imap_command_response (store, folder, &resp, cancellable, error)) == CAMEL_IMAP_RESPONSE_UNTAGGED && !camel_application_is_exiting) {
		GData *data;
		gchar *uid;
		guint32 flags;

		data = parse_fetch_response (imap_folder, resp);
		g_free (resp);
		resp = NULL;
		if (!data)
			continue;

		seq = GPOINTER_TO_INT (g_datalist_get_data (&data, "SEQUENCE"));
		uid = g_datalist_get_data (&data, "UID");
		flags = GPOINTER_TO_UINT (g_datalist_get_data (&data, "FLAGS"));

		if (!uid || !seq || seq > summary_len || seq < 0) {
			g_datalist_clear (&data);
			continue;
		}

		camel_operation_progress (
			cancellable, ++summary_got * 100 / summary_len);

		new[seq - 1].uid = g_strdup (uid);
		new[seq - 1].flags = flags;
		new[seq - 1].custom_flags = g_strdup (g_datalist_get_data (&data, "CUSTOM.FLAGS"));
		g_datalist_clear (&data);
	}

	if (summary_got == 0 && summary_len == 0) {
		camel_operation_pop_message (cancellable);
		g_free (new);
		g_free (resp);

		camel_folder_summary_free_array (known_uids);
		return TRUE;
	}

	camel_operation_pop_message (cancellable);

	if (type == CAMEL_IMAP_RESPONSE_ERROR || camel_application_is_exiting) {
		for (i = 0; i < summary_len && new[i].uid; i++) {
			g_free (new[i].uid);
			g_free (new[i].custom_flags);
		}
		g_free (new);
		g_free (resp);

		camel_folder_summary_free_array (known_uids);
		return TRUE;
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

	camel_folder_summary_prepare_fetch_all (folder->summary, NULL);

	for (i = 0, j = 0; i < summary_len && new[j].uid; i++) {
		gboolean changed = FALSE;

		uid = g_ptr_array_index (known_uids, i);
		if (!uid)
			continue;

		info = camel_folder_summary_get (folder->summary, uid);
		if (!info) {
			if (g_getenv ("CRASH_IMAP")) { /* Debug logs to tackle on hard to get imap crasher */
				printf (
					"CRASH: %s: %s",
					camel_folder_get_full_name (folder), uid);
				g_assert (0);
			} else
				continue;
		}

		iinfo = (CamelImapMessageInfo *) info;

		if (strcmp (uid, new[j].uid) != 0) {
			seq = i + 1 - del;
			del++;
			g_array_append_val (removed, seq);
			camel_message_info_free (info);
			continue;
		}

		/* Update summary flags */

		if (new[j].flags != iinfo->server_flags) {
			guint32 server_set, server_cleared;

			server_set = new[j].flags & ~iinfo->server_flags;
			server_cleared = iinfo->server_flags & ~new[j].flags;

			camel_message_info_set_flags ((CamelMessageInfo *) iinfo, server_set | server_cleared, (iinfo->info.flags | server_set) & ~server_cleared);
			iinfo->server_flags = new[j].flags;
			/* unset folder_flagged, because these are flags received froma server */
			iinfo->info.flags = iinfo->info.flags & (~CAMEL_MESSAGE_FOLDER_FLAGGED);
			iinfo->info.dirty = TRUE;
			if (info->summary)
				camel_folder_summary_touch (info->summary);
			changed = TRUE;
		}

		/* Do not merge custom flags when server doesn't support it.
		 * Because server always reports NULL, which means none, which
		 * will remove user's flags from local machine, which is bad.
		*/
		if ((folder->permanent_flags & CAMEL_MESSAGE_USER) != 0 &&
		    (iinfo->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED) == 0 &&
		    merge_custom_flags (info, new[j].custom_flags))
			changed = TRUE;

		if (changed) {
			if (changes == NULL)
				changes = camel_folder_change_info_new ();
			camel_folder_change_info_change_uid (changes, new[j].uid);
		}

		camel_message_info_free (info);
		g_free (new[j].uid);
		g_free (new[j].custom_flags);
		j++;
	}

	if (changes) {
		camel_folder_changed (folder, changes);
		camel_folder_change_info_free (changes);
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
		gint j;

		j = seq - del;
		g_array_append_val (removed, j);
	}

	/* And finally update the summary. */
	success = camel_imap_folder_changed (
		folder, exists, removed, cancellable, error);
	g_array_free (removed, TRUE);

	camel_folder_summary_free_array (known_uids);
	return success;
}

static const gchar *
get_message_uid (CamelFolder *folder,
                 CamelImapMessageInfo *info)
{
	const gchar *uid;

	g_return_val_if_fail (folder != NULL, NULL);
	g_return_val_if_fail (info != NULL, NULL);

	uid = camel_message_info_uid (info);
	g_return_val_if_fail (uid != NULL, NULL);

	if (!isdigit ((guchar) * uid)) {
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
get_matching (CamelFolder *folder,
              guint32 flags,
              guint32 mask,
              CamelMessageInfo *master_info,
              gchar **set,
              GPtrArray *summary,
              GPtrArray *deleted_uids,
              GPtrArray *junked_uids)
{
	GPtrArray *matches;
	CamelImapMessageInfo *info;
	gint i, max, range, last_range_uid;
	GString *gset;
	GList *list1 = NULL;
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
			info = (CamelImapMessageInfo *) camel_folder_summary_get (folder->summary, uid);
		} else
			continue;

		if (!info)
			continue;

		/* if the resulting flag list is empty, then "concat" other message
		 * only when server_flags are same, because there will be a flag removal
		 * command for this type of situation */
		if ((info->info.flags & mask) != flags || (flags == 0 && info->server_flags != ((CamelImapMessageInfo *) master_info)->server_flags)) {
			camel_message_info_free ((CamelMessageInfo *) info);
			close_range ();
			continue;
		}

		uid_num = atoi (uid);

		/* we got only changes, thus the uid's can be mixed up, not the consecutive list,
		 * thus close range if we are not in it */
		if (last_range_uid != -1 && uid_num != last_range_uid + 1) {
			close_range ();
		}

		/* only check user flags when we see other message than our 'master' */
		if (strcmp (master_info->uid, ((CamelMessageInfo *) info)->uid)) {
			const CamelFlag *flag;
			GList *list2 = NULL, *l1, *l2;
			gint count2 = 0, cmp = 0;

			if (!list1) {
				for (flag = camel_message_info_user_flags (master_info); flag; flag = flag->next) {
					if (*flag->name) {
						count1++;
						list1 = g_list_prepend (list1, (gchar *) flag->name);
					}
				}

				list1 = g_list_sort (list1, (GCompareFunc) strcmp);
			}

			for (flag = camel_message_info_user_flags (info); flag; flag = flag->next) {
				if (*flag->name) {
					count2++;
					list2 = g_list_prepend (list2, (gchar *) flag->name);
				}
			}

			if (count1 != count2) {
				g_list_free (list2);
				camel_message_info_free ((CamelMessageInfo *) info);
				close_range ();
				continue;
			}

			list2 = g_list_sort (list2, (GCompareFunc) strcmp);
			for (l1 = list1, l2 = list2; l1 && l2 && !cmp; l1 = l1->next, l2 = l2->next) {
				cmp = strcmp (l1->data, l2->data);
			}

			if (cmp) {
				g_list_free (list2);
				camel_message_info_free ((CamelMessageInfo *) info);
				close_range ();
				continue;
			}
		}

		if (deleted_uids && (info->info.flags & (CAMEL_MESSAGE_DELETED | CAMEL_IMAP_MESSAGE_MOVED)) == CAMEL_MESSAGE_DELETED) {
			g_ptr_array_add (deleted_uids, (gpointer) camel_pstring_strdup (camel_message_info_uid (info)));
			info->info.flags &= ~CAMEL_MESSAGE_DELETED;
		} else if (junked_uids && (info->info.flags & CAMEL_MESSAGE_JUNK) != 0) {
			g_ptr_array_add (junked_uids, (gpointer) camel_pstring_strdup (camel_message_info_uid (info)));
		}

		g_ptr_array_add (matches, info);
		/* Remove the uid from the list, to optimize*/
		camel_pstring_free (summary->pdata[i]);
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
		g_list_free (list1);

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

static gboolean
imap_sync_offline (CamelFolder *folder,
                   GError **error)
{
	CamelStore *parent_store;

	parent_store = camel_folder_get_parent_store (folder);

	if (folder->summary && (folder->summary->flags & CAMEL_FOLDER_SUMMARY_DIRTY) != 0) {
		CamelStoreInfo *si;
		const gchar *full_name;

		/* ... and store's summary when folder's summary is dirty */
		full_name = camel_folder_get_full_name (folder);
		si = camel_store_summary_path ((CamelStoreSummary *)((CamelImapStore *) parent_store)->summary, full_name);
		if (si) {
			if (si->total != camel_folder_summary_get_saved_count (folder->summary) || si->unread != camel_folder_summary_get_unread_count (folder->summary)) {
				si->total = camel_folder_summary_get_saved_count (folder->summary);
				si->unread = camel_folder_summary_get_unread_count (folder->summary);
				camel_store_summary_touch ((CamelStoreSummary *)((CamelImapStore *) parent_store)->summary);
			}

			camel_store_summary_info_free ((CamelStoreSummary *)((CamelImapStore *) parent_store)->summary, si);
		}
	}

	camel_folder_summary_save_to_db (folder->summary, NULL);
	camel_store_summary_save ((CamelStoreSummary *)((CamelImapStore *) parent_store)->summary);

	return TRUE;
}

static gboolean
host_ends_with (const gchar *host,
                const gchar *ends)
{
	gint host_len, ends_len;

	g_return_val_if_fail (host != NULL, FALSE);
	g_return_val_if_fail (ends != NULL, FALSE);

	host_len = strlen (host);
	ends_len = strlen (ends);

	return ends_len <= host_len && g_ascii_strcasecmp (host + host_len - ends_len, ends) == 0;
}

static gboolean
is_google_account (CamelStore *store)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelService *service;
	gboolean is_google;
	gchar *host;

	g_return_val_if_fail (store != NULL, FALSE);
	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);

	g_object_unref (settings);

	is_google =
		(host != NULL) && (
		host_ends_with (host, "gmail.com") ||
		host_ends_with (host, "googlemail.com"));

	g_free (host);

	return is_google;
}

static void
move_messages (CamelFolder *src_folder,
               GPtrArray *uids,
               CamelFolder *des_folder,
               GCancellable *cancellable,
               GError **error)
{
	g_return_if_fail (src_folder != NULL);

	/* it's OK to have these NULL */
	if (!uids || uids->len == 0 || des_folder == NULL)
		return;

	/* moving to the same folder means expunge only */
	if (src_folder != des_folder) {
		/* do 'copy' to not be bothered with CAMEL_MESSAGE_DELETED again */
		if (!imap_transfer_messages (
			src_folder, uids, des_folder, FALSE,
			NULL, FALSE, cancellable, error))
			return;
	}

	camel_imap_expunge_uids_only (src_folder, uids, cancellable, error);
}

static gboolean
imap_synchronize_sync (CamelFolder *folder,
                       gboolean expunge,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	CamelStore *parent_store;
	CamelImapStore *store;
	CamelImapMessageInfo *info;
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	gboolean is_gmail;
	CamelFolder *real_junk = NULL;
	CamelFolder *real_trash = NULL;
	gchar *folder_path;
	GError *local_error = NULL;

	GPtrArray *matches, *summary, *deleted_uids = NULL, *junked_uids = NULL;
	gchar *set, *flaglist, *uid;
	gint i, j, max;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_IMAP_STORE (parent_store);
	is_gmail = is_google_account (parent_store);

	service = CAMEL_SERVICE (parent_store);

	if (folder->permanent_flags == 0 || !camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		if (expunge) {
			if (!imap_expunge_sync (folder, cancellable, error))
				return FALSE;
		}
		return imap_sync_offline (folder, error);
	}

	/* write local changes first */
	replay_offline_journal (store, imap_folder, cancellable, NULL);

	/* Find a message with changed flags, find all of the other
	 * messages like it, sync them as a group, mark them as
	 * updated, and continue.
	 */
	summary = camel_folder_summary_get_changed (folder->summary); /* These should be in memory anyways */
	camel_folder_sort_uids (folder, summary);
	max = summary->len;

	settings = camel_service_ref_settings (service);

	/* deleted_uids is NULL when not using real trash */
	folder_path = camel_imap_settings_dup_real_trash_path (
		CAMEL_IMAP_SETTINGS (settings));
	if (folder_path != NULL && *folder_path) {
		if ((folder->folder_flags & CAMEL_FOLDER_IS_TRASH) != 0) {
			/* syncing the trash, expunge deleted when found any */
			real_trash = g_object_ref (folder);
		} else {
			real_trash = camel_store_get_trash_folder_sync (
				parent_store, cancellable, NULL);
		}
	}
	g_free (folder_path);

	/* junked_uids is NULL when not using real junk */
	folder_path = camel_imap_settings_dup_real_junk_path (
		CAMEL_IMAP_SETTINGS (settings));
	if (folder_path != NULL && *folder_path) {
		if ((folder->folder_flags & CAMEL_FOLDER_IS_JUNK) != 0) {
			/* syncing the junk, but cannot move
			 * messages to itself, thus do nothing */
			real_junk = NULL;
		} else {
			real_junk = camel_store_get_junk_folder_sync (
				parent_store, cancellable, NULL);
		}
	}
	g_free (folder_path);

	g_object_unref (settings);

	if (real_trash)
		deleted_uids = g_ptr_array_new ();

	if (real_junk)
		junked_uids = g_ptr_array_new ();

	for (i = 0; i < max; i++) {
		gboolean unset = FALSE;
		CamelImapResponse *response = NULL;

		uid = summary->pdata[i];

		if (!uid) /* Possibly it was sync by matching flags, which we NULLify */
			continue;

		if (!(info = (CamelImapMessageInfo *) camel_folder_summary_get (folder->summary, uid))) {
			continue;
		}

		if (!(info->info.flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			camel_message_info_free ((CamelMessageInfo *) info);
			continue;
		}

		/* Note: get_matching() uses UID_SET_LIMIT to limit
		 * the size of the uid-set string. We don't have to
		 * loop here to flush all the matching uids because
		 * they will be scooped up later by our parent loop (I
		 * think?). -- Jeff */
		matches = get_matching (
			folder, info->info.flags &
			(folder->permanent_flags | CAMEL_MESSAGE_FOLDER_FLAGGED),
			folder->permanent_flags | CAMEL_MESSAGE_FOLDER_FLAGGED,
			(CamelMessageInfo *) info, &set, summary,
			deleted_uids, junked_uids);
		if (matches == NULL) {
			camel_message_info_free (info);
			continue;
		}

		/* Make sure we're connected before issuing commands */
		if (!camel_imap_store_connected (store, NULL)) {
			g_free (set);
			camel_message_info_free (info);
			g_ptr_array_foreach (matches, (GFunc) camel_message_info_free, NULL);
			g_ptr_array_free (matches, TRUE);
			break;
		}

		if (deleted_uids && !is_gmail && (info->info.flags & CAMEL_MESSAGE_DELETED) != 0) {
			/* there is a real trash, do not set it on the server */
			info->info.flags &= ~CAMEL_MESSAGE_DELETED;
		}

		flaglist = imap_create_flag_list (info->info.flags & folder->permanent_flags, (CamelMessageInfo *) info, folder->permanent_flags);

		if (strcmp (flaglist, "()") == 0) {
			/* Note: Cyrus is broken and will not accept an
			 * empty-set of flags so... if this is true then we
			 * set and unset \Seen flag. It's necessary because
			 * we do not know the previously set user flags. */
			unset = TRUE;
			g_free (flaglist);

			/* unset all known server flags, because there left none in the actual flags */
			flaglist =  imap_create_flag_list (info->server_flags & folder->permanent_flags, (CamelMessageInfo *) info, folder->permanent_flags);

			if (strcmp (flaglist, "()") == 0) {
				/* this should not happen, really */
				g_free (flaglist);
				flaglist = strdup ("(\\Seen)");

				response = camel_imap_command (
					store, folder,
					cancellable, &local_error,
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
		if (local_error == NULL) {
			response = camel_imap_command (
				store, folder, cancellable, &local_error,
				"UID STORE %s %sFLAGS.SILENT %s",
				set, unset ? "-" : "", flaglist);
		}

		g_free (set);
		g_free (flaglist);

		if (response)
			camel_imap_response_free (store, response);

		if (local_error == NULL) {
			for (j = 0; j < matches->len; j++) {
				info = matches->pdata[j];
				if (deleted_uids && !is_gmail) {
					/* there is a real trash, do not keep this set */
					info->info.flags &= ~CAMEL_MESSAGE_DELETED;
				}

				info->info.flags &= ~(CAMEL_MESSAGE_FOLDER_FLAGGED | CAMEL_IMAP_MESSAGE_MOVED);
				((CamelImapMessageInfo *) info)->server_flags =	info->info.flags & CAMEL_IMAP_SERVER_FLAGS;
				info->info.dirty = TRUE; /* Sync it back to the DB */
				if (((CamelMessageInfo *) info)->summary)
					camel_folder_summary_touch (((CamelMessageInfo *) info)->summary);
			}
			camel_folder_summary_touch (folder->summary);
		}

		g_ptr_array_foreach (matches, (GFunc) camel_message_info_free, NULL);
		g_ptr_array_free (matches, TRUE);

		/* check for an exception */
		if (local_error != NULL) {
			g_propagate_error (error, local_error);
			if (deleted_uids) {
				g_ptr_array_foreach (deleted_uids, (GFunc) camel_pstring_free, NULL);
				g_ptr_array_free (deleted_uids, TRUE);
			}
			if (junked_uids) {
				g_ptr_array_foreach (junked_uids, (GFunc) camel_pstring_free, NULL);
				g_ptr_array_free (junked_uids, TRUE);
			}
			if (real_trash)
				g_object_unref (real_trash);
			if (real_junk)
				g_object_unref (real_junk);

			g_ptr_array_foreach (summary, (GFunc) camel_pstring_free, NULL);
			g_ptr_array_free (summary, TRUE);

			return FALSE;
		}
	}

	if (local_error == NULL)
		move_messages (
			folder, deleted_uids, real_trash,
			cancellable, &local_error);
	if (local_error == NULL)
		move_messages (
			folder, junked_uids, real_junk,
			cancellable, &local_error);

	if (deleted_uids) {
		g_ptr_array_foreach (deleted_uids, (GFunc) camel_pstring_free, NULL);
		g_ptr_array_free (deleted_uids, TRUE);
	}
	if (junked_uids) {
		g_ptr_array_foreach (junked_uids, (GFunc) camel_pstring_free, NULL);
		g_ptr_array_free (junked_uids, TRUE);
	}
	if (real_trash)
		g_object_unref (real_trash);
	if (real_junk)
		g_object_unref (real_junk);

	if (expunge && local_error == NULL)
		imap_expunge_sync (folder, cancellable, &local_error);

	if (local_error != NULL)
		g_propagate_error (error, local_error);

	g_ptr_array_foreach (summary, (GFunc) camel_pstring_free, NULL);
	g_ptr_array_free (summary, TRUE);

	/* Save the summary */
	return imap_sync_offline (folder, error);
}

static gint
uid_compar (gconstpointer va,
            gconstpointer vb)
{
	const gchar **sa = (const gchar **) va, **sb = (const gchar **) vb;
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

static gboolean
imap_expunge_uids_offline (CamelFolder *folder,
                           GPtrArray *uids,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelFolderChangeInfo *changes;
	CamelStore *parent_store;
	GList *list = NULL;
	const gchar *full_name;
	gint i;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	qsort (uids->pdata, uids->len, sizeof (gpointer), uid_compar);

	changes = camel_folder_change_info_new ();

	for (i = 0; i < uids->len; i++) {
		CamelMessageInfo *mi = camel_folder_summary_peek_loaded (folder->summary, uids->pdata[i]);

		if (mi) {
			camel_folder_summary_remove (folder->summary, mi);
			camel_message_info_free (mi);
		} else {
			camel_folder_summary_remove_uid (folder->summary, uids->pdata[i]);
		}

		camel_folder_change_info_remove_uid (changes, uids->pdata[i]);
		list = g_list_prepend (list, (gpointer) uids->pdata[i]);
		/* We intentionally don't remove it from the cache because
		 * the cached data may be useful in replaying a COPY later.
		 */
	}

	camel_db_delete_uids (parent_store->cdb_w, full_name, list, NULL);
	g_list_free (list);
	camel_folder_summary_save_to_db (folder->summary, NULL);

	camel_imap_journal_log (
		CAMEL_IMAP_FOLDER (folder)->journal,
		CAMEL_IMAP_JOURNAL_ENTRY_EXPUNGE, uids);

	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	return TRUE;
}

static gboolean
imap_expunge_uids_online (CamelFolder *folder,
                          GPtrArray *uids,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelImapStore *store;
	CamelImapResponse *response;
	gint uid = 0;
	gchar *set;
	gboolean full_expunge;
	CamelFolderChangeInfo *changes;
	CamelStore *parent_store;
	const gchar *full_name;
	gint i;
	GList *list = NULL;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	store = CAMEL_IMAP_STORE (parent_store);
	full_expunge = (store->capabilities & IMAP_CAPABILITY_UIDPLUS) == 0;

	if (!camel_imap_store_connected (store, error))
		return FALSE;

	if ((store->capabilities & IMAP_CAPABILITY_UIDPLUS) == 0) {
		if (!CAMEL_FOLDER_GET_CLASS (folder)->synchronize_sync (
			folder, 0, cancellable, error)) {
			return FALSE;
		}
	}

	qsort (uids->pdata, uids->len, sizeof (gpointer), uid_compar);

	while (uid < uids->len) {
		set = imap_uid_array_to_set (folder->summary, uids, uid, UID_SET_LIMIT, &uid);
		response = camel_imap_command (
			store, folder, cancellable, error,
			"UID STORE %s +FLAGS.SILENT (\\Deleted)", set);
		if (response)
			camel_imap_response_free (store, response);
		else {
			g_free (set);
			return FALSE;
		}

		if (!full_expunge) {
			GError *local_error = NULL;

			response = camel_imap_command (
				store, folder, cancellable, &local_error,
				"UID EXPUNGE %s", set);

			if (local_error != NULL) {
				g_clear_error (&local_error);

				/* UID EXPUNGE failed, something is broken on the server probably,
				 * thus fall back to the full expunge. It's not so good, especially
				 * when resyncing, it will remove already marked messages on the
				 * server too. I guess that's fine anyway, isn't it?
				 * For failed command see Gnome's bug #536486 */
				full_expunge = TRUE;
			}
		}

		if (full_expunge)
			response = camel_imap_command (store, folder, cancellable, NULL, "EXPUNGE");

		if (response)
			camel_imap_response_free (store, response);

		g_free (set);
	}

	changes = camel_folder_change_info_new ();
	for (i = 0; i < uids->len; i++) {
		CamelMessageInfo *mi = camel_folder_summary_peek_loaded (folder->summary, uids->pdata[i]);

		if (mi) {
			camel_folder_summary_remove (folder->summary, mi);
			camel_message_info_free (mi);
		} else {
			camel_folder_summary_remove_uid (folder->summary, uids->pdata[i]);
		}

		camel_folder_change_info_remove_uid (changes, uids->pdata[i]);
		list = g_list_prepend (list, (gpointer) uids->pdata[i]);
		/* We intentionally don't remove it from the cache because
		 * the cached data may be useful in replaying a COPY later.
		 */
	}

	camel_db_delete_uids (parent_store->cdb_w, full_name, list, NULL);
	g_list_free (list);
	camel_folder_summary_save_to_db (folder->summary, NULL);
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	return TRUE;
}

static gboolean
imap_expunge_sync (CamelFolder *folder,
                   GCancellable *cancellable,
                   GError **error)
{
	CamelStore *parent_store;
	GPtrArray *uids = NULL;
	const gchar *full_name;
	gboolean success, real_trash = FALSE;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	camel_folder_summary_save_to_db (folder->summary, NULL);

	if ((parent_store->flags & CAMEL_STORE_VTRASH) == 0) {
		CamelFolder *trash;
		GError *local_error = NULL;

		trash = camel_store_get_trash_folder_sync (
			parent_store, cancellable, &local_error);

		if (local_error == NULL && trash && (folder == trash || g_ascii_strcasecmp (full_name, camel_folder_get_full_name (trash)) == 0)) {
			/* it's a real trash folder, thus get all mails from there */
			real_trash = TRUE;
			uids = camel_folder_summary_get_array (folder->summary);
		}

		if (local_error != NULL)
			g_clear_error (&local_error);
	}

	if (!uids)
		uids = camel_db_get_folder_deleted_uids (parent_store->cdb_r, full_name, NULL);

	if (!uids)
		return TRUE;

	if (camel_offline_store_get_online (CAMEL_OFFLINE_STORE (parent_store)))
		success = imap_expunge_uids_online (
			folder, uids, cancellable, error);
	else
		success = imap_expunge_uids_offline (
			folder, uids, cancellable, error);

	if (real_trash) {
		camel_folder_summary_free_array (uids);
	} else {
		g_ptr_array_foreach (uids, (GFunc) camel_pstring_free, NULL);
		g_ptr_array_free (uids, TRUE);
	}

	return success;
}

gboolean
camel_imap_expunge_uids_resyncing (CamelFolder *folder,
                                   GPtrArray *uids,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelStore *parent_store;
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapStore *store;
	GPtrArray *keep_uids, *mark_uids;
	CamelImapResponse *response;
	gchar *result;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_IMAP_STORE (parent_store);

	if (imap_folder->read_only)
		return TRUE;

	if (!camel_imap_store_connected (store, error))
		return FALSE;

	if (store->capabilities & IMAP_CAPABILITY_UIDPLUS)
		return imap_expunge_uids_online (
			folder, uids, cancellable, error);

	/* If we don't have UID EXPUNGE we need to avoid expunging any
	 * of the wrong messages. So we search for deleted messages,
	 * and any that aren't in our to-expunge list get temporarily
	 * marked un-deleted.
	 */

	if (!CAMEL_FOLDER_GET_CLASS (folder)->synchronize_sync (
		folder, 0, cancellable, error))
		return FALSE;

	response = camel_imap_command (store, folder, cancellable, error, "UID SEARCH DELETED");
	if (!response)
		return FALSE;

	result = camel_imap_response_extract (store, response, "SEARCH", error);
	if (!result)
		return FALSE;

	if (result[8] == ' ') {
		gchar *uid, *lasts = NULL;
		gulong euid, kuid;
		gint ei, ki;

		keep_uids = g_ptr_array_new ();
		mark_uids = g_ptr_array_new ();

		/* Parse SEARCH response */
		for (uid = strtok_r (result + 9, " ", &lasts); uid; uid = strtok_r (NULL, " ", &lasts))
			g_ptr_array_add (keep_uids, uid);
		qsort (
			keep_uids->pdata, keep_uids->len,
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

			response = camel_imap_command (
				store, folder, cancellable, error,
				"UID STORE %s -FLAGS.SILENT (\\Deleted)",
				uidset);

			g_free (uidset);

			if (!response) {
				g_ptr_array_free (keep_uids, TRUE);
				g_ptr_array_free (mark_uids, TRUE);
				return FALSE;
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

			response = camel_imap_command (
				store, folder, cancellable, error,
				"UID STORE %s +FLAGS.SILENT (\\Deleted)",
				uidset);

			g_free (uidset);

			if (!response) {
				g_ptr_array_free (keep_uids, TRUE);
				g_ptr_array_free (mark_uids, TRUE);
				return FALSE;
			}
			camel_imap_response_free (store, response);
		}

		if (mark_uids != uids)
			g_ptr_array_free (mark_uids, TRUE);
	}

	/* Do the actual expunging */
	response = camel_imap_command (store, folder, cancellable, NULL, "EXPUNGE");
	if (response)
		camel_imap_response_free (store, response);

	/* And fix the remaining messages if we mangled them */
	if (keep_uids) {
		gchar *uidset;
		gint uid = 0;

		while (uid < keep_uids->len) {
			uidset = imap_uid_array_to_set (folder->summary, keep_uids, uid, UID_SET_LIMIT, &uid);

			response = camel_imap_command (
				store, folder, cancellable, NULL,
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

	return TRUE;
}

gboolean
camel_imap_expunge_uids_only (CamelFolder *folder,
                              GPtrArray *uids,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelStore *parent_store;

	g_return_val_if_fail (folder != NULL, FALSE);

	parent_store = camel_folder_get_parent_store (folder);
	g_return_val_if_fail (parent_store != NULL, FALSE);

	g_return_val_if_fail (uids != NULL, FALSE);

	if (camel_offline_store_get_online (CAMEL_OFFLINE_STORE (parent_store)))
		return camel_imap_expunge_uids_resyncing (
			folder, uids, cancellable, error);
	else
		return imap_expunge_uids_offline (
			folder, uids, cancellable, error);
}

static gchar *
get_temp_uid (void)
{
	gchar *res;

	static gint counter = 0;
	G_LOCK_DEFINE_STATIC (lock);

	G_LOCK (lock);
	res = g_strdup_printf (
		"tempuid-%lx-%d",
		(gulong) time (NULL),
		counter++);
	G_UNLOCK (lock);

	return res;
}

static gboolean
imap_append_offline (CamelFolder *folder,
                     CamelMimeMessage *message,
                     CamelMessageInfo *info,
                     gchar **appended_uid,
                     GError **error)
{
	CamelImapMessageCache *cache = CAMEL_IMAP_FOLDER (folder)->cache;
	CamelFolderChangeInfo *changes;
	gchar *uid;

	uid = get_temp_uid ();

	camel_imap_summary_add_offline (
		folder->summary, uid, message, info);
	CAMEL_IMAP_FOLDER_REC_LOCK (folder, cache_lock);
	camel_imap_message_cache_insert_wrapper (
		cache, uid, "", CAMEL_DATA_WRAPPER (message));
	CAMEL_IMAP_FOLDER_REC_UNLOCK (folder, cache_lock);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, uid);
	camel_folder_changed (folder, changes);
	camel_folder_change_info_free (changes);

	camel_imap_journal_log (
		CAMEL_IMAP_FOLDER (folder)->journal,
		CAMEL_IMAP_JOURNAL_ENTRY_APPEND, uid);
	if (appended_uid)
		*appended_uid = uid;
	else
		g_free (uid);

	return TRUE;
}

static void
imap_folder_add_ignore_recent (CamelImapFolder *imap_folder,
                               const gchar *uid)
{
	g_return_if_fail (imap_folder != NULL);
	g_return_if_fail (uid != NULL);

	if (!imap_folder->priv->ignore_recent)
		imap_folder->priv->ignore_recent = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);

	g_hash_table_insert (imap_folder->priv->ignore_recent, g_strdup (uid), GINT_TO_POINTER (1));
}

static gboolean
imap_folder_uid_in_ignore_recent (CamelImapFolder *imap_folder,
                                  const gchar *uid)
{
	g_return_val_if_fail (imap_folder != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	return imap_folder->priv->ignore_recent && g_hash_table_lookup (imap_folder->priv->ignore_recent, uid);
}

static CamelImapResponse *
do_append (CamelFolder *folder,
           CamelMimeMessage *message,
           CamelMessageInfo *info,
           gchar **uid,
           GCancellable *cancellable,
           GError **error)
{
	CamelStore *parent_store;
	CamelImapStore *store;
	CamelImapResponse *response, *response2;
	CamelStream *memstream;
	CamelMimeFilter *crlf_filter;
	CamelStream *streamfilter;
	GByteArray *ba;
	const gchar *full_name;
	gchar *flagstr, *end;
	guint32 flags = 0;
	GError *local_error = NULL;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_IMAP_STORE (parent_store);

	/* encode any 8bit parts so we avoid sending embedded nul-chars and such  */
	camel_mime_message_encode_8bit_parts (message);

	/* FIXME: We could avoid this if we knew how big the message was. */
	memstream = camel_stream_mem_new ();
	ba = g_byte_array_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (memstream), ba);

	streamfilter = camel_stream_filter_new (memstream);
	crlf_filter = camel_mime_filter_crlf_new (
		CAMEL_MIME_FILTER_CRLF_ENCODE,
		CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
	camel_stream_filter_add (
		CAMEL_STREAM_FILTER (streamfilter), crlf_filter);
	camel_data_wrapper_write_to_stream_sync (
		CAMEL_DATA_WRAPPER (message), streamfilter, cancellable, NULL);
	g_object_unref (streamfilter);
	g_object_unref (crlf_filter);
	g_object_unref (memstream);

	/* Some servers don't let us append with (CamelMessageInfo *)custom flags.  If the command fails for
	 * whatever reason, assume this is the case and save the state and try again */
retry:
	if (info) {
		flags = camel_message_info_flags (info);
	}

	flags &= folder->permanent_flags;
	if (flags)
		flagstr = imap_create_flag_list (flags, (CamelMessageInfo *) info, folder->permanent_flags);
	else
		flagstr = NULL;

	full_name = camel_folder_get_full_name (folder);
	response = camel_imap_command (
		store, NULL, cancellable, &local_error, "APPEND %F%s%s {%d}",
		full_name, flagstr ? " " : "",
		flagstr ? flagstr : "", ba->len);
	g_free (flagstr);

	if (!response) {
		if (g_error_matches (local_error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_INVALID) && !store->nocustomappend) {
			g_clear_error (&local_error);
			store->nocustomappend = 1;
			goto retry;
		}
		g_propagate_error (error, local_error);
		g_byte_array_free (ba, TRUE);
		return NULL;
	}

	if (*response->status != '+') {
		camel_imap_response_free (store, response);
		g_byte_array_free (ba, TRUE);
		return NULL;
	}

	/* send the rest of our data - the mime message */
	response2 = camel_imap_command_continuation (store, folder, (const gchar *) ba->data, ba->len, cancellable, error);
	g_byte_array_free (ba, TRUE);

	/* free it only after message is sent. This may cause more FETCHes. */
	camel_imap_response_free (store, response);
	if (!response2)
		return response2;

	if ((store->capabilities & IMAP_CAPABILITY_UIDPLUS) != 0 ||
	    is_google_account (parent_store)) {
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

static gboolean
imap_append_online (CamelFolder *folder,
                    CamelMimeMessage *message,
                    CamelMessageInfo *info,
                    gchar **appended_uid,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelStore *parent_store;
	CamelImapStore *store;
	CamelImapResponse *response;
	gboolean success = TRUE;
	gchar *uid;
	gint count;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_IMAP_STORE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		return imap_append_offline (
			folder, message, info, appended_uid, error);
	}

	if (!camel_imap_store_connected (store, error))
		return FALSE;

	count = camel_folder_summary_count (folder->summary);
	response = do_append (folder, message, info, &uid, cancellable, error);
	if (!response)
		return FALSE;

	if (uid) {
		/* Cache first, since freeing response may trigger a
		 * summary update that will want this information.
		 */
		CAMEL_IMAP_FOLDER_REC_LOCK (folder, cache_lock);
		camel_imap_message_cache_insert_wrapper (
			CAMEL_IMAP_FOLDER (folder)->cache, uid,
			"", CAMEL_DATA_WRAPPER (message));
		CAMEL_IMAP_FOLDER_REC_UNLOCK (folder, cache_lock);
		if (appended_uid)
			*appended_uid = uid;
		else
			g_free (uid);
	} else if (appended_uid)
		*appended_uid = NULL;

	camel_imap_response_free (store, response);

	/* Make sure a "folder_changed" is emitted. */
	if (store->current_folder != folder ||
	    camel_folder_summary_count (folder->summary) == count)
		success = imap_refresh_info_sync (folder, cancellable, error);

	return success;
}

gboolean
camel_imap_append_resyncing (CamelFolder *folder,
                             CamelMimeMessage *message,
                             CamelMessageInfo *info,
                             gchar **appended_uid,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelStore *parent_store;
	CamelImapStore *store;
	CamelImapResponse *response;
	gchar *uid;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_IMAP_STORE (parent_store);

	response = do_append (folder, message, info, &uid, cancellable, error);
	if (!response)
		return FALSE;

	if (uid) {
		CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
		const gchar *olduid = camel_message_info_uid (info);

		CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);
		camel_imap_message_cache_copy (
			imap_folder->cache, olduid,
			imap_folder->cache, uid);
		CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);

		if (appended_uid)
			*appended_uid = uid;
		else
			g_free (uid);
	} else if (appended_uid)
		*appended_uid = NULL;

	camel_imap_response_free (store, response);

	return TRUE;
}

static gboolean
imap_transfer_offline (CamelFolder *source,
                       GPtrArray *uids,
                       CamelFolder *dest,
                       gboolean delete_originals,
                       GPtrArray **transferred_uids,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelImapMessageCache *sc = CAMEL_IMAP_FOLDER (source)->cache;
	CamelImapMessageCache *dc = CAMEL_IMAP_FOLDER (dest)->cache;
	CamelFolderChangeInfo *changes;
	CamelMimeMessage *message;
	CamelMessageInfo *mi;
	gchar *uid, *destuid;
	gint i;
	GError *local_error = NULL;

	/* We grab the store's command lock first, and then grab the
	 * source and destination cache_locks. This way we can't
	 * deadlock in the case where we're simultaneously also trying
	 * to copy messages in the other direction from another thread.
	 */
	CAMEL_IMAP_FOLDER_REC_LOCK (source, cache_lock);
	CAMEL_IMAP_FOLDER_REC_LOCK (dest, cache_lock);

	if (transferred_uids) {
		*transferred_uids = g_ptr_array_new ();
		g_ptr_array_set_size (*transferred_uids, uids->len);
	}

	changes = camel_folder_change_info_new ();

	for (i = 0; i < uids->len && local_error == NULL; i++) {
		uid = uids->pdata[i];

		destuid = get_temp_uid ();

		mi = camel_folder_summary_get (source->summary, uid);
		g_return_val_if_fail (mi != NULL, FALSE);

		message = camel_folder_get_message_sync (
			source, uid, cancellable, &local_error);

		if (message) {
			camel_imap_summary_add_offline (
				dest->summary, destuid, message, mi);
			g_object_unref (message);
		} else
			camel_imap_summary_add_offline_uncached (
				dest->summary, destuid, mi);

		camel_imap_message_cache_copy (sc, uid, dc, destuid);
		camel_message_info_free (mi);

		camel_folder_change_info_add_uid (changes, destuid);
		if (transferred_uids)
			(*transferred_uids)->pdata[i] = destuid;
		else
			g_free (destuid);

		if (delete_originals && local_error == NULL)
			camel_folder_delete_message (source, uid);
	}

	CAMEL_IMAP_FOLDER_REC_UNLOCK (dest, cache_lock);
	CAMEL_IMAP_FOLDER_REC_UNLOCK (source, cache_lock);

	camel_folder_changed (dest, changes);
	camel_folder_change_info_free (changes);

	camel_imap_journal_log (
		CAMEL_IMAP_FOLDER (source)->journal,
		CAMEL_IMAP_JOURNAL_ENTRY_TRANSFER,
		dest, uids, delete_originals, NULL);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Call with lock held on destination and source folder cache */
static void
handle_copyuid (CamelImapResponse *response,
                CamelFolder *source,
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
		for (i = 0; i < src->len; i++) {
			camel_imap_message_cache_copy (
				scache, src->pdata[i],
				dcache, dest->pdata[i]);

			imap_folder_add_ignore_recent (CAMEL_IMAP_FOLDER (destination), dest->pdata[i]);
		}

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

/* Call with lock held on destination and source folder cache */
static void
handle_copyuid_copy_user_tags (CamelImapResponse *response,
                               CamelFolder *source,
                               CamelFolder *destination,
                               GCancellable *cancellable)
{
	CamelStore *parent_store;
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

	/* first do NOOP on the destination folder, so server has enough time to propagate our copy command there */
	parent_store = camel_folder_get_parent_store (destination);
	camel_imap_response_free (
		CAMEL_IMAP_STORE (parent_store), camel_imap_command (
		CAMEL_IMAP_STORE (parent_store), destination, cancellable, NULL, "NOOP"));

	/* refresh folder's summary first, we copied messages there on the server,
	 * but do not know about it in a local summary */
	if (!imap_refresh_info_sync (destination, cancellable, NULL))
		goto lose;

	src = imap_uid_set_to_array (source->summary, srcset);
	dest = imap_uid_set_to_array (destination->summary, destset);

	if (src && dest && src->len == dest->len) {
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
any_has_user_tag (CamelFolder *source,
                  gchar *uidset)
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

static gboolean
do_copy (CamelFolder *source,
         GPtrArray *uids,
         CamelFolder *destination,
         gint delete_originals,
         GCancellable *cancellable,
         GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	CamelStore *parent_store;
	CamelImapStore *store;
	CamelImapResponse *response;
	const gchar *full_name;
	gchar *trash_path;
	gchar *uidset;
	gint uid = 0, last = 0, i;
	GError *local_error = NULL;
	gboolean mark_moved;
	gboolean success = TRUE;

	parent_store = camel_folder_get_parent_store (source);
	store = CAMEL_IMAP_STORE (parent_store);

	if (!camel_imap_store_connected (store, error))
		return FALSE;

	service = CAMEL_SERVICE (parent_store);

	settings = camel_service_ref_settings (service);

	trash_path = camel_imap_settings_dup_real_trash_path (
		CAMEL_IMAP_SETTINGS (settings));

	g_object_unref (settings);

	mark_moved = is_google_account (parent_store) && trash_path != NULL;

	full_name = camel_folder_get_full_name (destination);

	while (uid < uids->len && local_error == NULL) {
		uidset = imap_uid_array_to_set (source->summary, uids, uid, UID_SET_LIMIT, &uid);

		/* use XGWMOVE only when none of the moving messages has set any user tag */
		if ((store->capabilities & IMAP_CAPABILITY_XGWMOVE) != 0 && delete_originals && !any_has_user_tag (source, uidset)) {
			response = camel_imap_command (
				store, source, cancellable, &local_error,
				"UID XGWMOVE %s %F", uidset, full_name);
			/* returns only 'A00012 OK UID XGWMOVE completed' '* 2 XGWMOVE' so nothing useful */
			camel_imap_response_free (store, response);
		} else {
			CAMEL_IMAP_FOLDER_REC_LOCK (source, cache_lock);
			CAMEL_IMAP_FOLDER_REC_LOCK (destination, cache_lock);
			response = camel_imap_command (
				store, source, cancellable, &local_error,
				"UID COPY %s %F", uidset, full_name);
			if (response && (store->capabilities & IMAP_CAPABILITY_UIDPLUS))
				handle_copyuid (response, source, destination);
			if (response)
				handle_copyuid_copy_user_tags (
					response, source, destination,
					cancellable);
			camel_imap_response_free (store, response);
			CAMEL_IMAP_FOLDER_REC_UNLOCK (destination, cache_lock);
			CAMEL_IMAP_FOLDER_REC_UNLOCK (source, cache_lock);
		}

		if (local_error == NULL && delete_originals && (mark_moved || !trash_path)) {
			for (i = last; i < uid; i++) {
				camel_folder_delete_message (
					source, uids->pdata[i]);
				if (mark_moved) {
					CamelMessageInfoBase *info = (CamelMessageInfoBase *) camel_folder_summary_get (source->summary, uids->pdata[i]);

					if (info)
						info->flags |= CAMEL_IMAP_MESSAGE_MOVED;
				}
			}
			last = uid;
		}
		g_free (uidset);
	}

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		success = FALSE;

	/* There is a real trash folder set, which is not on a google account
	 * and copied messages should be deleted, thus do not move them into
	 * a trash folder, but just expunge them, because the copy part of
	 * the operation was successful. */
	} else if (trash_path && !mark_moved && delete_originals)
		camel_imap_expunge_uids_only (source, uids, cancellable, NULL);

	g_free (trash_path);

	return success;
}

static gboolean
imap_transfer_messages (CamelFolder *source,
                        GPtrArray *uids,
                        CamelFolder *dest,
                        gboolean delete_originals,
                        GPtrArray **transferred_uids,
                        gboolean can_call_sync,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelStore *parent_store;
	CamelImapStore *store;
	gboolean success = TRUE;
	gint count;

	parent_store = camel_folder_get_parent_store (source);
	store = CAMEL_IMAP_STORE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		return imap_transfer_offline (
			source, uids, dest, delete_originals,
			transferred_uids, cancellable, error);

	/* Sync message flags if needed. */
	if (can_call_sync && !imap_synchronize_sync (
		source, FALSE, cancellable, error))
		return FALSE;

	count = camel_folder_summary_count (dest->summary);

	qsort (uids->pdata, uids->len, sizeof (gpointer), uid_compar);

	/* Now copy the messages */
	if (!do_copy (source, uids, dest, delete_originals, cancellable, error))
		return FALSE;

	/* Make the destination notice its new messages */
	if (store->current_folder != dest ||
	    camel_folder_summary_count (dest->summary) == count)
		success = imap_refresh_info_sync (dest, cancellable, error);

	/* FIXME */
	if (transferred_uids)
		*transferred_uids = NULL;

	return success;
}

static gboolean
imap_transfer_online (CamelFolder *source,
                      GPtrArray *uids,
                      CamelFolder *dest,
                      gboolean delete_originals,
                      GPtrArray **transferred_uids,
                      GCancellable *cancellable,
                      GError **error)
{
	return imap_transfer_messages (
		source, uids, dest, delete_originals,
		transferred_uids, TRUE, cancellable, error);
}

gboolean
camel_imap_transfer_resyncing (CamelFolder *source,
                               GPtrArray *uids,
                               CamelFolder *dest,
                               gboolean delete_originals,
                               GPtrArray **transferred_uids,
                               GCancellable *cancellable,
                               GError **error)
{
	GPtrArray *realuids;
	gint first, i;
	const gchar *uid;
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	GError *local_error = NULL;

	qsort (uids->pdata, uids->len, sizeof (gpointer), uid_compar);

	/*This is trickier than append_resyncing, because some of
	 * the messages we are copying may have been copied or
	 * appended into @source while we were offline, in which case
	 * if we don't have UIDPLUS, we won't know their real UIDs,
	 * so we'll have to append them rather than copying. */

	realuids = g_ptr_array_new ();

	i = 0;
	while (i < uids->len && local_error == NULL) {
		 /* Skip past real UIDs  */
		for (first = i; i < uids->len; i++) {
			uid = uids->pdata[i];

			if (!isdigit ((guchar) * uid)) {
				uid = camel_imap_journal_uidmap_lookup ((CamelIMAPJournal *) CAMEL_IMAP_FOLDER (source)->journal, uid);
				if (!uid)
					break;
			}
			g_ptr_array_add (realuids, (gchar *) uid);
		}

		/* If we saw any real UIDs, do a COPY */
		if (i != first) {
			do_copy (
				source, realuids, dest, delete_originals,
				cancellable, &local_error);
			g_ptr_array_set_size (realuids, 0);
			if (i == uids->len || local_error != NULL)
				break;
		}

		/* Deal with fake UIDs */
		while (i < uids->len &&
		       !isdigit (*(guchar *)(uids->pdata[i])) &&
		       local_error == NULL) {
			uid = uids->pdata[i];
			message = camel_folder_get_message_sync (
				source, uid, cancellable, NULL);
			if (!message) {
				/* Message must have been expunged */
				i++;
				continue;
			}
			info = camel_folder_get_message_info (source, uid);
			g_return_val_if_fail (info != NULL, FALSE);

			imap_append_online (
				dest, message, info,
				NULL, cancellable, &local_error);
			camel_folder_free_message_info (source, info);
			g_object_unref (message);
			if (delete_originals && local_error == NULL)
				camel_folder_delete_message (source, uid);
			i++;
		}
	}

	g_ptr_array_free (realuids, FALSE);

	/* FIXME */
	if (transferred_uids)
		*transferred_uids = NULL;

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static GPtrArray *
imap_search_by_expression (CamelFolder *folder,
                           const gchar *expression,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	GPtrArray *matches;

	/* we could get around this by creating a new search object each time,
	 * but i doubt its worth it since any long operation would lock the
	 * command channel too */
	CAMEL_IMAP_FOLDER_LOCK (folder, search_lock);

	camel_folder_search_set_folder (imap_folder->search, folder);
	matches = camel_folder_search_search (imap_folder->search, expression, NULL, cancellable, error);

	CAMEL_IMAP_FOLDER_UNLOCK (folder, search_lock);

	return matches;
}

static guint32
imap_count_by_expression (CamelFolder *folder,
                          const gchar *expression,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	guint32 matches;

	/* we could get around this by creating a new search object each time,
	 * but i doubt its worth it since any long operation would lock the
	 * command channel too */
	CAMEL_IMAP_FOLDER_LOCK (folder, search_lock);

	camel_folder_search_set_folder (imap_folder->search, folder);
	matches = camel_folder_search_count (imap_folder->search, expression, cancellable, error);

	CAMEL_IMAP_FOLDER_UNLOCK (folder, search_lock);

	return matches;
}

static GPtrArray *
imap_search_by_uids (CamelFolder *folder,
                     const gchar *expression,
                     GPtrArray *uids,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	GPtrArray *matches;

	if (uids->len == 0)
		return g_ptr_array_new ();

	CAMEL_IMAP_FOLDER_LOCK (folder, search_lock);

	camel_folder_search_set_folder (imap_folder->search, folder);
	matches = camel_folder_search_search (imap_folder->search, expression, uids, cancellable, error);

	CAMEL_IMAP_FOLDER_UNLOCK (folder, search_lock);

	return matches;
}

static void
imap_search_free (CamelFolder *folder,
                  GPtrArray *uids)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);

	g_return_if_fail (imap_folder->search);

	CAMEL_IMAP_FOLDER_LOCK (folder, search_lock);

	camel_folder_search_free_result (imap_folder->search, uids);

	CAMEL_IMAP_FOLDER_UNLOCK (folder, search_lock);
}

static CamelMimeMessage *get_message (CamelImapFolder *imap_folder,
				      const gchar *uid,
				      CamelMessageContentInfo *ci,
				      GCancellable *cancellable,
				      GError **error);

struct _part_spec_stack {
	struct _part_spec_stack *parent;
	gint part;
};

static void
part_spec_push (struct _part_spec_stack **stack,
                gint part)
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
get_content (CamelImapFolder *imap_folder,
             const gchar *uid,
             CamelMimePart *part,
             CamelMessageContentInfo *ci,
             gint frommsg,
             GCancellable *cancellable,
             GError **error)
{
	CamelDataWrapper *content = NULL;
	CamelStream *stream;
	gchar *part_spec;

	part_spec = content_info_get_part_spec (ci);

	d (printf ("get content '%s' '%s' (frommsg = %d)\n", part_spec, camel_content_type_format (ci->type), frommsg));

	/* There are three cases: multipart/signed, multipart, message/rfc822, and "other" */
	if (camel_content_type_is (ci->type, "multipart", "signed")) {
		CamelMultipartSigned *body_mp;
		gchar *spec;
		gboolean success;

		/* Note: because we get the content parts uninterpreted anyway, we could potentially
		 * just use the normalmultipart code, except that multipart/signed wont let you yet! */

		body_mp = camel_multipart_signed_new ();
		/* need to set this so it grabs the boundary and other info about the signed type */
		/* we assume that part->content_type is more accurate/full than ci->type */
		camel_data_wrapper_set_mime_type_field (CAMEL_DATA_WRAPPER (body_mp), CAMEL_DATA_WRAPPER (part)->mime_type);

		spec = g_alloca (strlen (part_spec) + 6);
		if (frommsg)
			sprintf (spec, part_spec[0] ? "%s.TEXT" : "TEXT", part_spec);
		else
			strcpy (spec, part_spec);
		g_free (part_spec);

		stream = camel_imap_folder_fetch_data (imap_folder, uid, spec, FALSE, cancellable, error);
		if (stream) {
			success = camel_data_wrapper_construct_from_stream_sync (
				CAMEL_DATA_WRAPPER (body_mp), stream, cancellable, error);
			g_object_unref (stream);
			if (!success) {
				g_object_unref ( body_mp);
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
		isdigest = camel_content_type_is (((CamelDataWrapper *) part)->mime_type, "multipart", "digest");

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
			stream = camel_imap_folder_fetch_data (imap_folder, uid, child_spec, FALSE, cancellable, error);
			if (stream) {
				gboolean success;

				part = camel_mime_part_new ();
				success = camel_data_wrapper_construct_from_stream_sync (
					CAMEL_DATA_WRAPPER (part), stream, cancellable, error);
				g_object_unref (stream);
				if (!success) {
					g_object_unref (part);
					g_object_unref (body_mp);
					g_free (child_spec);
					return NULL;
				}

				content = get_content (imap_folder, uid, part, ci, FALSE, cancellable, error);
			}

			if (!stream || !content) {
				g_object_unref (body_mp);
				g_free (child_spec);
				return NULL;
			}

			if (camel_debug ("imap:folder")) {
				gchar *ct = camel_content_type_format (camel_mime_part_get_content_type ((CamelMimePart *) part));
				gchar *ct2 = camel_content_type_format (ci->type);

				printf ("Setting part content type to '%s' contentinfo type is '%s'\n", ct, ct2);
				g_free (ct);
				g_free (ct2);
			}

			/* if we had no content-type header on a multipart/digest sub-part, then we need to
			 * treat it as message/rfc822 instead */
			if (isdigest && camel_medium_get_header ((CamelMedium *) part, "content-type") == NULL) {
				CamelContentType *ct = camel_content_type_new ("message", "rfc822");

				camel_data_wrapper_set_mime_type_field (content, ct);
				camel_content_type_unref (ct);
			} else {
				camel_data_wrapper_set_mime_type_field (content, camel_mime_part_get_content_type (part));
			}

			camel_medium_set_content (CAMEL_MEDIUM (part), content);
			g_object_unref (content);

			camel_multipart_add_part (body_mp, part);
			g_object_unref (part);

			ci = ci->next;
		}

		g_free (child_spec);

		return (CamelDataWrapper *) body_mp;
	} else if (camel_content_type_is (ci->type, "message", "rfc822")) {
		content = (CamelDataWrapper *) get_message (imap_folder, uid, ci->childs, cancellable, error);
		g_free (part_spec);
		return content;
	} else {
		CamelTransferEncoding enc;
		gchar *spec;

		/* NB: we need this differently to multipart/signed case above on purpose */
		spec = g_alloca (strlen (part_spec) + 6);
		if (frommsg)
			sprintf (spec, part_spec[0] ? "%s.1" : "1", part_spec);
		else
			strcpy (spec, part_spec[0]?part_spec:"1");

		enc = ci->encoding ? camel_transfer_encoding_from_string (ci->encoding) : CAMEL_TRANSFER_ENCODING_DEFAULT;
		content = camel_imap_wrapper_new (imap_folder, ci->type, enc, uid, spec, part);
		g_free (part_spec);
		return content;
	}
}

static CamelMimeMessage *
get_message (CamelImapFolder *imap_folder,
             const gchar *uid,
             CamelMessageContentInfo *ci,
             GCancellable *cancellable,
             GError **error)
{
	CamelFolder *folder;
	CamelStore *parent_store;
	CamelImapStore *store;
	CamelDataWrapper *content;
	CamelMimeMessage *msg;
	CamelStream *stream;
	gchar *section_text, *part_spec;
	gboolean success;

	folder = CAMEL_FOLDER (imap_folder);
	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_IMAP_STORE (parent_store);

	part_spec = content_info_get_part_spec (ci);
	d (printf ("get message '%s'\n", part_spec));
	section_text = g_strdup_printf (
		"%s%s%s", part_spec, *part_spec ? "." : "",
					store->server_level >= IMAP_LEVEL_IMAP4REV1 ? "HEADER" : "0");

	stream = camel_imap_folder_fetch_data (imap_folder, uid, section_text, FALSE, cancellable, error);
	g_free (section_text);
	g_free (part_spec);
	if (!stream)
		return NULL;

	msg = camel_mime_message_new ();
	success = camel_data_wrapper_construct_from_stream_sync (
		CAMEL_DATA_WRAPPER (msg), stream, cancellable, error);
	g_object_unref (stream);
	if (!success) {
		g_object_unref (msg);
		return NULL;
	}

	content = get_content (imap_folder, uid, CAMEL_MIME_PART (msg), ci, TRUE, cancellable, error);
	if (!content) {
		g_object_unref (msg);
		return NULL;
	}

	if (camel_debug ("imap:folder")) {
		gchar *ct = camel_content_type_format (camel_mime_part_get_content_type ((CamelMimePart *) msg));
		gchar *ct2 = camel_content_type_format (ci->type);

		printf ("Setting message content type to '%s' contentinfo type is '%s'\n", ct, ct2);
		g_free (ct);
		g_free (ct2);
	}

	camel_data_wrapper_set_mime_type_field (content, camel_mime_part_get_content_type ((CamelMimePart *) msg));
	camel_medium_set_content (CAMEL_MEDIUM (msg), content);
	g_object_unref (content);

	return msg;
}

#define IMAP_SMALL_BODY_SIZE 5120

static CamelMimeMessage *
get_message_simple (CamelImapFolder *imap_folder,
                    const gchar *uid,
                    CamelStream *stream,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelMimeMessage *msg;
	gboolean success;

	if (!stream) {
		stream = camel_imap_folder_fetch_data (
			imap_folder, uid, "",
			FALSE, cancellable, error);
		if (!stream)
			return NULL;
	}

	msg = camel_mime_message_new ();
	success = camel_data_wrapper_construct_from_stream_sync (
		CAMEL_DATA_WRAPPER (msg), stream, cancellable, error);
	g_object_unref (stream);
	if (!success) {
		g_prefix_error (error, _("Unable to retrieve message: "));
		g_object_unref (msg);
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
		for (ci = ci->childs; ci; ci = ci->next)
			if (content_info_incomplete (ci))
				return TRUE;
	}

	return FALSE;
}

static CamelImapMessageInfo *
imap_folder_summary_uid_or_error (CamelFolderSummary *summary,
                                  const gchar *uid,
                                  GError **error)
{
	CamelImapMessageInfo *mi;
	mi = (CamelImapMessageInfo *) camel_folder_summary_get (summary, uid);
	if (mi == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("Cannot get message with message ID %s: %s"),
			uid, _("No such message available."));
	}
	return mi;
}

CamelMimeMessage *
imap_get_message_cached (CamelFolder *folder,
                         const gchar *message_uid,
                         GCancellable *cancellable)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelMimeMessage *msg = NULL;
	CamelStream *stream;

	stream = camel_imap_folder_fetch_data (imap_folder, message_uid, "", TRUE, cancellable, NULL);
	if (stream != NULL)
		msg = get_message_simple (imap_folder, message_uid, stream, cancellable, NULL);

	return msg;
}

static CamelMimeMessage *
imap_get_message_sync (CamelFolder *folder,
                       const gchar *uid,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelStore *parent_store;
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapStore *store;
	CamelImapMessageInfo *mi;
	CamelMimeMessage *msg = NULL;
	gint retry;
	GError *local_error = NULL;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_IMAP_STORE (parent_store);

	mi = imap_folder_summary_uid_or_error (folder->summary, uid, error);
	if (!mi)
		return NULL;

	/* If its cached in full, just get it as is, this is only a shortcut,
	 * since we get stuff from the cache anyway.  It affects a busted
	 * connection though. */
	msg = imap_get_message_cached (folder, uid, cancellable);
	if (msg != NULL)
		goto done;

	if (!camel_imap_store_connected (store, error))
		return NULL;

	/* All this mess is so we silently retry a fetch if we fail with
	 * service_unavailable, without an (equivalent) mess of gotos */
	retry = 0;
	do {
		retry++;
		g_clear_error (&local_error);

		/* If the message is small or only 1 part, or server doesn't do 4v1 (properly) fetch it in one piece. */
		if (store->server_level < IMAP_LEVEL_IMAP4REV1
		    || store->braindamaged
		    || mi->info.size < IMAP_SMALL_BODY_SIZE
		    || (!content_info_incomplete (mi->info.content) && !mi->info.content->childs)) {
			CamelMessageInfoBase *info = (CamelMessageInfoBase *) camel_folder_summary_get (folder->summary, uid);
			msg = get_message_simple (imap_folder, uid, NULL, cancellable, &local_error);
			if (info && !info->preview && msg && camel_folder_summary_get_need_preview (folder->summary)) {
				if (camel_mime_message_build_preview ((CamelMimePart *) msg, (CamelMessageInfo *) info) && info->preview)
					camel_folder_summary_add_preview (folder->summary, (CamelMessageInfo *) info);
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

				if (!camel_imap_store_connected (store, NULL)) {
					g_set_error (
						error, CAMEL_SERVICE_ERROR,
						CAMEL_SERVICE_ERROR_UNAVAILABLE,
						_("This message is not currently available"));
					goto fail;
				}

				response = camel_imap_command (store, folder, cancellable, &local_error, "UID FETCH %s BODY", uid);

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
					g_clear_error (&local_error);
				}
			}

			if (camel_debug_start ("imap:folder")) {
				printf ("Folder get message '%s' folder info ->\n", uid);
				camel_message_info_dump ((CamelMessageInfo *) mi);
				camel_debug_end ();
			}

			/* FETCH returned OK, but we didn't parse a BODY
			 * response. Courier will return invalid BODY
			 * responses for invalidly MIMEd messages, so
			 * fall back to fetching the entire thing and
			 * let the mailer's "bad MIME" code handle it.
			 */
			if (content_info_incomplete (mi->info.content))
				msg = get_message_simple (imap_folder, uid, NULL, cancellable, &local_error);
			else
				msg = get_message (imap_folder, uid, mi->info.content, cancellable, &local_error);
			if (msg && camel_folder_summary_get_need_preview (folder->summary)) {
				CamelMessageInfoBase *info = (CamelMessageInfoBase *) camel_folder_summary_get (folder->summary, uid);
				if (info && !info->preview) {
					if (camel_mime_message_build_preview ((CamelMimePart *) msg, (CamelMessageInfo *) info) && info->preview)
						camel_folder_summary_add_preview (folder->summary, (CamelMessageInfo *) info);
				}
				camel_message_info_free (info);
			}

		}
	} while (msg == NULL
		 && retry < 2
		 && g_error_matches (local_error, CAMEL_SERVICE_ERROR, CAMEL_SERVICE_ERROR_UNAVAILABLE));

done:
	if (msg) {
		gboolean has_attachment;

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
		if (((camel_message_info_flags ((CamelMessageInfo *) mi) & CAMEL_MESSAGE_ATTACHMENTS) && !has_attachment) ||
		    ((camel_message_info_flags ((CamelMessageInfo *) mi) & CAMEL_MESSAGE_ATTACHMENTS) == 0 && has_attachment)) {
			camel_message_info_set_flags ((CamelMessageInfo *) mi, CAMEL_MESSAGE_ATTACHMENTS, has_attachment ? CAMEL_MESSAGE_ATTACHMENTS : 0);
		}
	}

	if (local_error != NULL)
		g_propagate_error (error, local_error);

fail:
	camel_message_info_free (&mi->info);

	return msg;
}

/**
 * imap_synchronize_message_sync
 *
 * Ensure that a message is cached locally, but don't retrieve the content if
 * it is already local.
 */
static gboolean
imap_synchronize_message_sync (CamelFolder *folder,
                               const gchar *uid,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapMessageInfo *mi;
	CamelMimeMessage *msg = NULL;
	CamelStream *stream = NULL;
	gboolean success = FALSE;

	mi = imap_folder_summary_uid_or_error (folder->summary, uid, error);
	if (!mi)
	  /* No such UID - is this duplicate work? The sync process selects
	   * UIDs to start with.
	   */
	  return FALSE;
	camel_message_info_free (&mi->info);

	/* If we can get a stream, assume its fully cached. This may be false
	 * if partial streams are saved elsewhere in the code - but that seems
	 * best solved by knowning more about whether a given message is fully
	 * available locally or not,
	 */
	/* If its cached in full, just get it as is, this is only a shortcut,
	 * since we get stuff from the cache anyway.  It affects a busted connection though. */
	if ((stream = camel_imap_folder_fetch_data (imap_folder, uid, "", TRUE, cancellable, NULL))) {
		g_object_unref (stream);
		return TRUE;
	}
	msg = imap_get_message_sync (folder, uid, cancellable, error);
	if (msg != NULL) {
		g_object_unref (msg);
		success = TRUE;
	}

	return success;
}

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
decode_time (const guchar **in,
             gint *hour,
             gint *min,
             gint *sec)
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

	date = camel_mktime_utc (&tm);

	/* date is now GMT of the time we want, but not offset by the timezone ... */

	/* this should convert the time to the GMT equiv time */
	date -= ((n / 100) * 60 * 60) + (n % 100) * 60;

	return date;
}

static void
add_message_from_data (CamelFolder *folder,
                       GPtrArray *messages,
                       gint first,
                       GData *data,
                       GCancellable *cancellable)
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
	if (!camel_data_wrapper_construct_from_stream_sync (
		CAMEL_DATA_WRAPPER (msg), stream, cancellable, NULL)) {
		g_object_unref (msg);
		return;
	}

	bodystructure = g_datalist_get_data (&data, "BODY");

	mi = (CamelImapMessageInfo *)
		camel_folder_summary_info_new_from_message (
		folder->summary, msg, bodystructure);
	g_object_unref (msg);

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
construct_junk_headers (gchar *header,
                        gchar *value,
                        struct _junk_data *jdata)
{
	gchar *bs, *es, *flag = NULL;
	gchar *bdata = g_datalist_get_data (&(jdata->data), "BODY_PART_DATA");
	struct _camel_header_param *node;

	/* FIXME: This can be written in a much clever way.
	 * We can create HEADERS file or carry all headers till filtering so
	 * that header based filtering can be much faster. But all that later. */
	bs = camel_strstrcase (bdata ? bdata:"", header);
	if (bs) {
		bs += strlen (header);
		bs = strchr (bs, ':');
		if (bs) {
			bs++;
			while (*bs == ' ')
				bs++;
			es = strchr (bs, '\n');
			if (es)
				flag = g_strndup (bs, es - bs);
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

#define CAMEL_MESSAGE_INFO_HEADERS "DATE FROM TO CC SUBJECT REFERENCES IN-REPLY-TO MESSAGE-ID MIME-VERSION CONTENT-TYPE CONTENT-CLASS X-CALENDAR-ATTACHMENT "

/* FIXME: this needs to be kept in sync with camel-mime-utils.c's list
 * of mailing-list headers and so might be best if this were
 * auto-generated? */
#define MAILING_LIST_HEADERS "X-MAILING-LIST X-LOOP LIST-ID LIST-POST MAILING-LIST ORIGINATOR X-LIST SENDER RETURN-PATH X-BEENTHERE "

static gboolean
imap_update_summary (CamelFolder *folder,
                     gint exists,
                     CamelFolderChangeInfo *changes,
                     GCancellable *cancellable,
                     GError **error)
{
	CamelStore *parent_store;
	CamelService *service;
	CamelSettings *settings;
	CamelImapStore *store;
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	GPtrArray *fetch_data = NULL, *messages = NULL, *needheaders;
	CamelFetchHeadersType fetch_headers;
	gchar **extra_headers;
	guint32 flags, uidval;
	gint i, seq, first, size, got;
	CamelImapResponseType type;
	GString *header_spec = NULL;
	CamelImapMessageInfo *mi;
	CamelStream *stream;
	gchar *uid, *resp, *tempuid;
	GData *data;
	gint k = 0, ct;
	gboolean success = TRUE;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_IMAP_STORE (parent_store);
	service = CAMEL_SERVICE (parent_store);

	if (!camel_imap_store_connected (store, error))
		return FALSE;

	settings = camel_service_ref_settings (service);

	fetch_headers = camel_imap_settings_get_fetch_headers (
		CAMEL_IMAP_SETTINGS (settings));

	extra_headers = camel_imap_settings_dup_fetch_headers_extra (
		CAMEL_IMAP_SETTINGS (settings));

	g_object_unref (settings);

	if (store->server_level >= IMAP_LEVEL_IMAP4REV1) {
		if (fetch_headers == CAMEL_FETCH_HEADERS_ALL)
			header_spec = g_string_new ("HEADER");
		else {
			gchar *temp;
			header_spec = g_string_new ("HEADER.FIELDS (");
			g_string_append (header_spec, CAMEL_MESSAGE_INFO_HEADERS);
			if (fetch_headers == CAMEL_FETCH_HEADERS_BASIC_AND_MAILING_LIST)
				g_string_append (header_spec, MAILING_LIST_HEADERS);
			if (extra_headers != NULL) {
				guint length, ii;

				length = g_strv_length ((gchar **) extra_headers);
				for (ii = 0; ii < length; ii++) {
					g_string_append (header_spec, extra_headers[ii]);
					if (ii + 1 < length)
						g_string_append_c (header_spec, ' ');
				}
			}

			temp = g_string_free (header_spec, FALSE);
			temp = g_strstrip (temp);
			header_spec = g_string_new (temp);
			g_free (temp);
			g_string_append (header_spec, ")");
		}
	} else
		header_spec = g_string_new ("0");

	g_strfreev (extra_headers);

	d (printf ("Header is : %s", header_spec->str));

	/* Figure out if any of the new messages are already cached (which
	 * may be the case if we're re-syncing after disconnected operation).
	 * If so, get their UIDs, FLAGS, and SIZEs. If not, get all that
	 * and ask for the headers too at the same time.
	 */
	seq = camel_folder_summary_count (folder->summary);
	first = seq + 1;
	if (seq > 0) {
		GPtrArray *known_uids;

		known_uids = camel_folder_summary_get_array (folder->summary);
		if (known_uids) {
			camel_folder_sort_uids (folder, known_uids);

			tempuid = g_ptr_array_index (known_uids, seq - 1);
			if (tempuid)
				uidval = strtoul (tempuid, NULL, 10);
			else
				uidval = 0;

			camel_folder_summary_free_array (known_uids);
		} else
			uidval = 0;
	} else
		uidval = 0;

	got = 0;
	if (!camel_imap_command_start (store, folder, cancellable, error,
				       "UID FETCH %d:* (FLAGS RFC822.SIZE INTERNALDATE BODYSTRUCTURE BODY.PEEK[%s])",
				       uidval + 1, header_spec->str)) {
		g_string_free (header_spec, TRUE);
		return FALSE;
	}

	camel_operation_push_message (
		cancellable,
		_("Fetching summary information for new messages in %s"),
		camel_folder_get_display_name (folder));

	/* Parse the responses. We can't add a message to the summary
	 * until we've gotten its headers, and there's no guarantee
	 * the server will send the responses in a useful order...
	 */
	fetch_data = g_ptr_array_new ();
	messages = g_ptr_array_new ();
	ct = exists - seq;
	while ((type = camel_imap_command_response (store, folder, &resp, cancellable, error)) ==
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
			add_message_from_data (
				folder, messages, first, data, cancellable);
			g_datalist_set_data (&data, "BODY_PART_STREAM", NULL);
		}

		camel_operation_progress (cancellable, k * 100 / ct);

		g_ptr_array_add (fetch_data, data);
	}

	camel_operation_pop_message (cancellable);

	if (type == CAMEL_IMAP_RESPONSE_ERROR || camel_application_is_exiting) {
		g_string_free (header_spec, TRUE);
		success = FALSE;
		goto finish;
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

		qsort (
			needheaders->pdata, needheaders->len,
			sizeof (gpointer), uid_compar);

		camel_operation_push_message (
			cancellable,
			_("Fetching summary information for new messages in %s"),
			camel_folder_get_display_name (folder));

		while (uid < needheaders->len && !camel_application_is_exiting) {
			uidset = imap_uid_array_to_set (folder->summary, needheaders, uid, UID_SET_LIMIT, &uid);
			if (!camel_imap_command_start (store, folder, cancellable, error,
						       "UID FETCH %s BODYSTRUCTURE BODY.PEEK[%s]",
						       uidset, header_spec->str)) {
				g_ptr_array_free (needheaders, TRUE);
				g_free (uidset);
				g_string_free (header_spec, TRUE);
				success = FALSE;
				break;
			}
			g_free (uidset);

			while ((type = camel_imap_command_response (store, folder, &resp, cancellable, error))
			       == CAMEL_IMAP_RESPONSE_UNTAGGED && !camel_application_is_exiting) {
				data = parse_fetch_response (imap_folder, resp);
				g_free (resp);
				if (!data)
					continue;

				stream = g_datalist_get_data (&data, "BODY_PART_STREAM");
				if (stream) {
					add_message_from_data (
						folder, messages, first,
						data, cancellable);
					got += IMAP_PRETEND_SIZEOF_HEADERS;
					camel_operation_progress (
						cancellable, got * 100 / size);
				}
				g_datalist_clear (&data);
			}

			if (type == CAMEL_IMAP_RESPONSE_ERROR || camel_application_is_exiting) {
				g_ptr_array_free (needheaders, TRUE);
				g_string_free (header_spec, TRUE);
				success = FALSE;

				break;
			}
		}
		camel_operation_pop_message (cancellable);
	}

	g_ptr_array_free (needheaders, TRUE);
	g_string_free (header_spec, TRUE);

 finish:
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

			mi = (CamelImapMessageInfo *) camel_message_info_clone (pmi);
		}

		uid = g_datalist_get_data (&data, "UID");
		if (uid)
			mi->info.uid = camel_pstring_strdup (uid);
		flags = GPOINTER_TO_INT (g_datalist_get_data (&data, "FLAGS"));
		if (flags) {
			gchar *custom_flags = NULL;

			((CamelImapMessageInfo *) mi)->server_flags = flags;
			/* "or" them in with the existing flags that may
			 * have been set by summary_info_new_from_message.
			 */
			mi->info.flags |= flags;

			custom_flags = g_datalist_get_data (&data, "CUSTOM.FLAGS");
			if (custom_flags)
				fillup_custom_flags ((CamelMessageInfo *) mi, custom_flags);
		}
		size = GPOINTER_TO_INT (g_datalist_get_data (&data, "RFC822.SIZE"));
		if (size)
			mi->info.size = size;

		/* Just do this to build the junk required headers to be built*/
		jdata.data = data;
		jdata.mi = (CamelMessageInfoBase *) mi;
		g_hash_table_foreach ((GHashTable *) camel_session_get_junk_headers (camel_service_get_session (service)), (GHFunc) construct_junk_headers, &jdata);
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
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Incomplete server response: "
				"no information provided for message %d"),
				i + first);
			continue;
		}
		uid = (gchar *) camel_message_info_uid (mi);
		if (uid[0] == 0) {
			g_warning ("Server provided no uid: message %d", i + first);
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Incomplete server response: "
				"no UID provided for message %d"),
				i + first);
			continue;
		}

		/* FIXME: If it enters if (info) it will always match the exception. So stupid */
		/* FIXME[disk-summary] Use a db query to see if the DB exists */
/*		info = (CamelImapMessageInfo *)camel_folder_summary_get (folder->summary, uid); */
/*		if (info) { */
/*			for (seq = 0; seq < camel_folder_summary_count (folder->summary); seq++) { */
/*				if (folder->summary->messages->pdata[seq] == info) */
/*					break; */
/*			} */

		((CamelMessageInfoBase *) mi)->dirty = TRUE;
		if (((CamelMessageInfoBase *) mi)->summary)
			camel_folder_summary_touch (((CamelMessageInfoBase *) mi)->summary);
		camel_folder_summary_add (folder->summary, (CamelMessageInfo *) mi);
		camel_folder_change_info_add_uid (changes, camel_message_info_uid (mi));

		/* Report all new messages as recent, even without that flag, thus new
		 * messages will be filtered even after saw by other software earlier.
		 * Only skip those which we added ourself, like after drag&drop to this folder. */
		if (!imap_folder_uid_in_ignore_recent (imap_folder, camel_message_info_uid (mi))
		    && ((mi->info.flags & CAMEL_IMAP_MESSAGE_RECENT) != 0 || getenv ("FILTER_RECENT") == NULL))
			camel_folder_change_info_recent_uid (changes, camel_message_info_uid (mi));

	}

	g_ptr_array_free (messages, TRUE);

	if (imap_folder->priv->ignore_recent) {
		g_hash_table_unref (imap_folder->priv->ignore_recent);
		imap_folder->priv->ignore_recent = NULL;
	}

	return success;

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
				camel_message_info_free (messages->pdata[i]);
		}
		g_ptr_array_free (messages, TRUE);
	}

	if (imap_folder->priv->ignore_recent) {
		g_hash_table_unref (imap_folder->priv->ignore_recent);
		imap_folder->priv->ignore_recent = NULL;
	}

	return FALSE;
}

/* Called with the store's connect_lock locked */
gboolean
camel_imap_folder_changed (CamelFolder *folder,
                           gint exists,
                           GArray *expunged,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelFolderChangeInfo *changes;
	gint len;
	gboolean success = TRUE;

	changes = camel_folder_change_info_new ();
	if (expunged) {
		CamelStore *parent_store;
		gint i, id;
		GList *deleted = NULL;
		const gchar *full_name;
		const gchar *uid;
		GPtrArray *known_uids;

		known_uids = camel_folder_summary_get_array (folder->summary);
		camel_folder_sort_uids (folder, known_uids);
		for (i = 0; i < expunged->len; i++) {
			CamelMessageInfo *mi;

			id = g_array_index (expunged, int, i);
			uid = id - 1 + i >= 0 && id - 1 + i < known_uids->len ? g_ptr_array_index (known_uids, id - 1 + i) : NULL;
			if (uid == NULL) {
				/* FIXME: danw: does this mean that the summary is corrupt? */
				/* I guess a message that we never retrieved got expunged? */
				continue;
			}

			deleted = g_list_prepend (deleted, (gpointer) uid);
			camel_folder_change_info_remove_uid (changes, uid);
			CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);
			camel_imap_message_cache_remove (imap_folder->cache, uid);
			CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);

			mi = camel_folder_summary_peek_loaded (folder->summary, uid);
			if (mi) {
				camel_folder_summary_remove (folder->summary, mi);
				camel_message_info_free (mi);
			} else {
				camel_folder_summary_remove_uid (folder->summary, uid);
			}
		}

		/* Delete all in one transaction */
		full_name = camel_folder_get_full_name (folder);
		parent_store = camel_folder_get_parent_store (folder);
		camel_db_delete_uids (parent_store->cdb_w, full_name, deleted, NULL);
		g_list_free (deleted);

		camel_folder_summary_free_array (known_uids);
	}

	len = camel_folder_summary_count (folder->summary);
	if (exists > len && !camel_application_is_exiting)
		success = imap_update_summary (
			folder, exists, changes, cancellable, error);

	camel_folder_summary_save_to_db (folder->summary, NULL);
	if (camel_folder_change_info_changed (changes))
		camel_folder_changed (folder, changes);

	camel_folder_change_info_free (changes);

	return success;
}

static void
imap_thaw (CamelFolder *folder)
{
	CamelImapFolder *imap_folder;

	CAMEL_FOLDER_CLASS (camel_imap_folder_parent_class)->thaw (folder);
	if (camel_folder_is_frozen (folder))
		return;

	/* FIXME imap_refresh_info_sync() may block, but camel_folder_thaw()
	 *       is not supposed to block.  Potential hang here. */
	imap_folder = CAMEL_IMAP_FOLDER (folder);
	if (imap_folder->need_refresh) {
		imap_folder->need_refresh = FALSE;
		imap_refresh_info_sync (folder, NULL, NULL);
	}
}

CamelStream *
camel_imap_folder_fetch_data (CamelImapFolder *imap_folder,
                              const gchar *uid,
                              const gchar *section_text,
                              gboolean cache_only,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelFolder *folder = CAMEL_FOLDER (imap_folder);
	CamelStore *parent_store;
	CamelImapStore *store;
	CamelImapResponse *response;
	CamelStream *stream;
	GData *fetch_data;
	gchar *found_uid;
	gint i;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_IMAP_STORE (parent_store);

	if (!cache_only && !camel_imap_store_connected (store, error))
		return NULL;

	/* EXPUNGE responses have to modify the cache, which means
	 * they have to grab the cache_lock while holding the
	 * connect_lock.
	 *
	 * Because getting the service lock may cause MUCH unecessary
	 * delay when we already have the data locally, we do the
	 * locking separately.  This could cause a race
	 * getting the same data from the cache, but that is only
	 * an inefficiency, and bad luck.
	 */
	CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);
	stream = camel_imap_message_cache_get (imap_folder->cache, uid, section_text, NULL);
	if (!stream && (!strcmp (section_text, "HEADER") || !strcmp (section_text, "0"))) {
		stream = camel_imap_message_cache_get (imap_folder->cache, uid, "", NULL);
	}
	CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);

	if (stream || cache_only)
		return stream;

	CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);

	if (!camel_imap_store_connected (store, NULL)) {
		g_set_error (
			error, CAMEL_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("This message is not currently available"));
		CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);
		return NULL;
	}

	if (store->server_level < IMAP_LEVEL_IMAP4REV1 && !*section_text) {
		response = camel_imap_command (
			store, folder, cancellable, error,
			"UID FETCH %s RFC822.PEEK", uid);
	} else {
		response = camel_imap_command (
			store, folder, cancellable, error,
			"UID FETCH %s BODY.PEEK[%s]", uid,
			section_text);
	}

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
		g_set_error (
			error, CAMEL_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Could not find message body in FETCH response."));
	} else {
		g_object_ref (stream);
		g_datalist_clear (&fetch_data);
	}

	return stream;
}

static GData *
parse_fetch_response (CamelImapFolder *imap_folder,
                      gchar *response)
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
			CamelMessageFlags flags;
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
			stream = camel_imap_message_cache_insert (
				imap_folder->cache,
				uid, part_spec,
				body, body_len, NULL, NULL);
			CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);
			if (stream == NULL)
				stream = camel_stream_mem_new_with_buffer (body, body_len);
		}

		if (stream)
			g_datalist_set_data_full (
				&data, "BODY_PART_STREAM", stream,
				(GDestroyNotify) g_object_unref);
	}

	return data;
}

/* it uses connect_lock, thus be sure it doesn't run in main thread */
static CamelFolderQuotaInfo *
imap_get_quota_info_sync (CamelFolder *folder,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelStore *parent_store;
	CamelImapStore *imap_store;
	CamelImapResponse *response;
	CamelFolderQuotaInfo *res = NULL, *last = NULL;

	parent_store = camel_folder_get_parent_store (folder);
	imap_store = CAMEL_IMAP_STORE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (imap_store)))
		return NULL;

	if (!camel_imap_store_connected (imap_store, NULL))
		return NULL;

	if (imap_store->capabilities & IMAP_CAPABILITY_QUOTA) {
		const gchar *full_name = camel_folder_get_full_name (folder);
		CamelImapStoreNamespace *ns = camel_imap_store_summary_namespace_find_full (imap_store->summary, full_name);
		gchar *folder_name = camel_imap_store_summary_path_to_full (imap_store->summary, full_name, ns ? ns->sep : '/');

		response = camel_imap_command (imap_store, NULL, cancellable, error, "GETQUOTAROOT \"%s\"", folder_name);

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
						g_debug ("Unexpected quota response '%s'; skipping it...", (const gchar *) response->untagged->pdata[i]);
				}
			}
			camel_imap_response_free (imap_store, response);
		}

		g_free (folder_name);
	}

	return res;
}

/**
 * Scan for messages that are local and return the rest.
 */
static GPtrArray *
imap_get_uncached_uids (CamelFolder *folder,
                        GPtrArray *uids,
                        GError **error)
{
	GPtrArray *result;
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);

	CAMEL_IMAP_FOLDER_REC_LOCK (imap_folder, cache_lock);

	result = camel_imap_message_cache_filter_cached (
		imap_folder->cache, uids, error);

	CAMEL_IMAP_FOLDER_REC_UNLOCK (imap_folder, cache_lock);

	return result;
}


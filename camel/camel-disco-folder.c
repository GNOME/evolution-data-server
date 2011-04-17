/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-disco-folder.c: abstract class for a disconnectable folder */

/*
 * Authors: Dan Winship <danw@ximian.com>
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

#include <glib/gi18n-lib.h>

#include "camel-debug.h"
#include "camel-disco-folder.h"
#include "camel-disco-store.h"
#include "camel-session.h"

struct _CamelDiscoFolderPrivate {
	gboolean offline_sync;
};

struct _cdf_sync_msg {
	CamelSessionThreadMsg msg;

	CamelFolder *folder;
	CamelFolderChangeInfo *changes;
};

/* The custom property ID is a CamelArg artifact.
 * It still identifies the property in state files. */
enum {
	PROP_0,
	PROP_OFFLINE_SYNC = 0x2400
};

G_DEFINE_TYPE (CamelDiscoFolder, camel_disco_folder, CAMEL_TYPE_FOLDER)

static void
cdf_sync_offline (CamelSession *session, CamelSessionThreadMsg *mm)
{
	struct _cdf_sync_msg *m = (struct _cdf_sync_msg *)mm;
	gint i;

	camel_operation_push_message (
		mm->cancellable,
		_("Downloading new messages for offline mode"));

	if (m->changes) {
		for (i=0;i<m->changes->uid_added->len;i++) {
			gint pc = i * 100 / m->changes->uid_added->len;

			camel_operation_progress (mm->cancellable, pc);
			camel_disco_folder_cache_message ((CamelDiscoFolder *)m->folder,
							 m->changes->uid_added->pdata[i],
							 NULL, &mm->error);
		}
	} else {
		camel_disco_folder_prepare_for_offline ((CamelDiscoFolder *)m->folder,
						       "(match-all)",
						       NULL, &mm->error);
	}

	camel_operation_pop_message (mm->cancellable);
}

static void
cdf_sync_free (CamelSession *session, CamelSessionThreadMsg *mm)
{
	struct _cdf_sync_msg *m = (struct _cdf_sync_msg *)mm;

	if (m->changes)
		camel_folder_change_info_free (m->changes);
	g_object_unref (m->folder);
}

static CamelSessionThreadOps cdf_sync_ops = {
	cdf_sync_offline,
	cdf_sync_free,
};

static void
cdf_folder_changed (CamelFolder *folder,
                    CamelFolderChangeInfo *changes)
{
	CamelService *parent_service;
	CamelStore *parent_store;
	CamelURL *url;
	gboolean offline_sync;

	parent_store = camel_folder_get_parent_store (folder);

	parent_service = CAMEL_SERVICE (parent_store);
	url = camel_service_get_camel_url (parent_service);

	offline_sync = camel_disco_folder_get_offline_sync (
		CAMEL_DISCO_FOLDER (folder));

	if (changes->uid_added->len > 0 && (offline_sync
		|| camel_url_get_param (url, "offline_sync"))) {
		CamelSession *session;
		struct _cdf_sync_msg *m;

		session = camel_service_get_session (parent_service);
		m = camel_session_thread_msg_new (session, &cdf_sync_ops, sizeof (*m));
		m->changes = camel_folder_change_info_new ();
		camel_folder_change_info_cat (m->changes, changes);
		m->folder = g_object_ref (folder);
		camel_session_thread_queue (session, &m->msg, 0);
	}
}

static void
disco_folder_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_OFFLINE_SYNC:
			camel_disco_folder_set_offline_sync (
				CAMEL_DISCO_FOLDER (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
disco_folder_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_OFFLINE_SYNC:
			g_value_set_boolean (
				value, camel_disco_folder_get_offline_sync (
				CAMEL_DISCO_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static gboolean
disco_expunge_uids (CamelFolder *folder,
                    GPtrArray *uids,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelDiscoFolderClass *disco_folder_class;
	CamelStore *parent_store;
	gboolean success;

	if (uids->len == 0)
		return TRUE;

	parent_store = camel_folder_get_parent_store (folder);
	disco_folder_class = CAMEL_DISCO_FOLDER_GET_CLASS (folder);

	switch (camel_disco_store_status (CAMEL_DISCO_STORE (parent_store))) {
	case CAMEL_DISCO_STORE_ONLINE:
		g_return_val_if_fail (disco_folder_class->expunge_uids_online != NULL, FALSE);
		success = disco_folder_class->expunge_uids_online (
			folder, uids, error);
		CAMEL_CHECK_GERROR (
			folder, expunge_uids_online, success, error);
		return success;

	case CAMEL_DISCO_STORE_OFFLINE:
		g_return_val_if_fail (disco_folder_class->expunge_uids_offline != NULL, FALSE);
		success = disco_folder_class->expunge_uids_offline (
			folder, uids, error);
		CAMEL_CHECK_GERROR (
			folder, expunge_uids_offline, success, error);
		return success;

	case CAMEL_DISCO_STORE_RESYNCING:
		g_return_val_if_fail (disco_folder_class->expunge_uids_resyncing != NULL, FALSE);
		success = disco_folder_class->expunge_uids_resyncing (
			folder, uids, error);
		CAMEL_CHECK_GERROR (
			folder, expunge_uids_resyncing, success, error);
		return success;
	}

	g_return_val_if_reached (FALSE);
}

static gboolean
disco_append_message_sync (CamelFolder *folder,
                           CamelMimeMessage *message,
                           CamelMessageInfo *info,
                           gchar **appended_uid,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelDiscoFolderClass *disco_folder_class;
	CamelStore *parent_store;
	gboolean success;

	parent_store = camel_folder_get_parent_store (folder);
	disco_folder_class = CAMEL_DISCO_FOLDER_GET_CLASS (folder);

	switch (camel_disco_store_status (CAMEL_DISCO_STORE (parent_store))) {
	case CAMEL_DISCO_STORE_ONLINE:
		g_return_val_if_fail (disco_folder_class->append_online != NULL, FALSE);
		success = disco_folder_class->append_online (
			folder, message, info,
			appended_uid, cancellable, error);
		CAMEL_CHECK_GERROR (folder, append_online, success, error);
		return success;

	case CAMEL_DISCO_STORE_OFFLINE:
		g_return_val_if_fail (disco_folder_class->append_offline != NULL, FALSE);
		success = disco_folder_class->append_offline (
			folder, message, info,
			appended_uid, cancellable, error);
		CAMEL_CHECK_GERROR (folder, append_offline, success, error);
		return success;

	case CAMEL_DISCO_STORE_RESYNCING:
		g_return_val_if_fail (disco_folder_class->append_resyncing != NULL, FALSE);
		success = disco_folder_class->append_resyncing (
			folder, message, info,
			appended_uid, cancellable, error);
		CAMEL_CHECK_GERROR (folder, append_resyncing, success, error);
		return success;
	}

	g_return_val_if_reached (FALSE);
}

static gboolean
disco_expunge_sync (CamelFolder *folder,
                    GCancellable *cancellable,
                    GError **error)
{
	GPtrArray *uids;
	gint i;
	guint count;
	CamelMessageInfo *info;
	gboolean success;

	uids = g_ptr_array_new ();
	camel_folder_summary_prepare_fetch_all (folder->summary, NULL);
	count = camel_folder_summary_count (folder->summary);
	for (i = 0; i < count; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		if (camel_message_info_flags (info) & CAMEL_MESSAGE_DELETED)
			g_ptr_array_add (uids, g_strdup (camel_message_info_uid (info)));
		camel_message_info_free (info);
	}

	success = disco_expunge_uids (folder, uids, cancellable, error);

	for (i = 0; i < uids->len; i++)
		g_free (uids->pdata[i]);
	g_ptr_array_free (uids, TRUE);

	return success;
}

static gboolean
disco_refresh_info_sync (CamelFolder *folder,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelDiscoFolderClass *disco_folder_class;
	CamelStore *parent_store;
	gboolean success;

	parent_store = camel_folder_get_parent_store (folder);

	if (camel_disco_store_status (CAMEL_DISCO_STORE (parent_store)) != CAMEL_DISCO_STORE_ONLINE)
		return TRUE;

	disco_folder_class = CAMEL_DISCO_FOLDER_GET_CLASS (folder);
	g_return_val_if_fail (disco_folder_class->refresh_info_online != NULL, FALSE);

	success = disco_folder_class->refresh_info_online (
		folder, cancellable, error);
	CAMEL_CHECK_GERROR (folder, refresh_info_online, success, error);

	return success;
}

static gboolean
disco_synchronize_sync (CamelFolder *folder,
                        gboolean expunge,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelDiscoFolderClass *disco_folder_class;
	CamelStore *parent_store;
	gboolean success;

	if (expunge && !disco_expunge_sync (folder, cancellable, error))
		return FALSE;

	camel_object_state_write (CAMEL_OBJECT (folder));

	parent_store = camel_folder_get_parent_store (folder);
	disco_folder_class = CAMEL_DISCO_FOLDER_GET_CLASS (folder);

	switch (camel_disco_store_status (CAMEL_DISCO_STORE (parent_store))) {
	case CAMEL_DISCO_STORE_ONLINE:
		g_return_val_if_fail (disco_folder_class->sync_online != NULL, FALSE);
		success = disco_folder_class->sync_online (folder, error);
		CAMEL_CHECK_GERROR (folder, sync_online, success, error);
		return success;

	case CAMEL_DISCO_STORE_OFFLINE:
		g_return_val_if_fail (disco_folder_class->sync_offline != NULL, FALSE);
		success = disco_folder_class->sync_offline (folder, error);
		CAMEL_CHECK_GERROR (folder, sync_offline, success, error);
		return success;

	case CAMEL_DISCO_STORE_RESYNCING:
		g_return_val_if_fail (disco_folder_class->sync_resyncing != NULL, FALSE);
		success = disco_folder_class->sync_resyncing (folder, error);
		CAMEL_CHECK_GERROR (folder, sync_resyncing, success, error);
		return success;
	}

	g_return_val_if_reached (FALSE);
}

static gboolean
disco_transfer_messages_to_sync (CamelFolder *source,
                                 GPtrArray *uids,
                                 CamelFolder *dest,
                                 gboolean delete_originals,
                                 GPtrArray **transferred_uids,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelDiscoFolderClass *disco_folder_class;
	CamelStore *parent_store;
	gboolean success;

	parent_store = camel_folder_get_parent_store (source);
	disco_folder_class = CAMEL_DISCO_FOLDER_GET_CLASS (source);

	switch (camel_disco_store_status (CAMEL_DISCO_STORE (parent_store))) {
	case CAMEL_DISCO_STORE_ONLINE:
		g_return_val_if_fail (disco_folder_class->transfer_online != NULL, FALSE);
		success = disco_folder_class->transfer_online (
			source, uids, dest, transferred_uids,
			delete_originals, cancellable, error);
		CAMEL_CHECK_GERROR (source, transfer_online, success, error);
		return success;

	case CAMEL_DISCO_STORE_OFFLINE:
		g_return_val_if_fail (disco_folder_class->transfer_offline != NULL, FALSE);
		success = disco_folder_class->transfer_offline (
			source, uids, dest, transferred_uids,
			delete_originals, cancellable, error);
		CAMEL_CHECK_GERROR (source, transfer_offline, success, error);
		return success;

	case CAMEL_DISCO_STORE_RESYNCING:
		g_return_val_if_fail (disco_folder_class->transfer_resyncing != NULL, FALSE);
		success = disco_folder_class->transfer_resyncing (
			source, uids, dest, transferred_uids,
			delete_originals, cancellable, error);
		CAMEL_CHECK_GERROR (source, transfer_resyncing, success, error);
		return success;
	}

	g_return_val_if_reached (FALSE);
}

static gboolean
disco_prepare_for_offline (CamelDiscoFolder *disco_folder,
                           const gchar *expression,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelFolder *folder = CAMEL_FOLDER (disco_folder);
	GPtrArray *uids;
	gint i;
	gboolean success = TRUE;

	camel_operation_push_message (
		cancellable, _("Preparing folder '%s' for offline"),
		camel_folder_get_full_name (folder));

	if (expression)
		uids = camel_folder_search_by_expression (folder, expression, error);
	else
		uids = camel_folder_get_uids (folder);

	if (!uids) {
		camel_operation_pop_message (cancellable);
		return FALSE;
	}

	for (i = 0; i < uids->len && success; i++) {
		camel_operation_progress (
			cancellable, (i * 100) / uids->len);
		success = camel_disco_folder_cache_message (
			disco_folder, uids->pdata[i], cancellable, error);
	}

	if (expression)
		camel_folder_search_free (folder, uids);
	else
		camel_folder_free_uids (folder, uids);

	camel_operation_pop_message (cancellable);

	return success;
}

static gboolean
disco_refresh_info_online (CamelFolder *folder,
                           GCancellable *cancellable,
                           GError **error)
{
	return TRUE;
}

static void
camel_disco_folder_class_init (CamelDiscoFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	g_type_class_add_private (class, sizeof (CamelDiscoFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = disco_folder_set_property;
	object_class->get_property = disco_folder_get_property;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->append_message_sync = disco_append_message_sync;
	folder_class->expunge_sync = disco_expunge_sync;
	folder_class->refresh_info_sync = disco_refresh_info_sync;
	folder_class->synchronize_sync = disco_synchronize_sync;
	folder_class->transfer_messages_to_sync = disco_transfer_messages_to_sync;

	class->prepare_for_offline = disco_prepare_for_offline;
	class->refresh_info_online = disco_refresh_info_online;

	g_object_class_install_property (
		object_class,
		PROP_OFFLINE_SYNC,
		g_param_spec_boolean (
			"offline-sync",
			"Offline Sync",
			N_("Copy folder content locally for offline operation"),
			FALSE,
			G_PARAM_READWRITE |
			CAMEL_PARAM_PERSISTENT));
}

static void
camel_disco_folder_init (CamelDiscoFolder *disco_folder)
{
	disco_folder->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		disco_folder, CAMEL_TYPE_DISCO_FOLDER,
		CamelDiscoFolderPrivate);

	g_signal_connect (
		disco_folder, "changed",
		G_CALLBACK (cdf_folder_changed), NULL);
}

/**
 * camel_disco_folder_get_offline_sync:
 * @disco_folder: a #CamelDiscoFolder
 *
 * Since: 2.32
 **/
gboolean
camel_disco_folder_get_offline_sync (CamelDiscoFolder *disco_folder)
{
	g_return_val_if_fail (CAMEL_IS_DISCO_FOLDER (disco_folder), FALSE);

	return disco_folder->priv->offline_sync;
}

/**
 * camel_disco_folder_set_offline_sync:
 * @disco_folder: a #CamelDiscoFolder
 * @offline_sync: whether to synchronize for offline use
 *
 * Since: 2.32
 **/
void
camel_disco_folder_set_offline_sync (CamelDiscoFolder *disco_folder,
                                     gboolean offline_sync)
{
	g_return_if_fail (CAMEL_IS_DISCO_FOLDER (disco_folder));

	disco_folder->priv->offline_sync = offline_sync;

	g_object_notify (G_OBJECT (disco_folder), "offline-sync");
}

/**
 * camel_disco_folder_expunge_uids:
 * @folder: a (disconnectable) folder
 * @uids: array of UIDs to expunge
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * This expunges the messages in @uids from @folder. It should take
 * whatever steps are needed to avoid expunging any other messages,
 * although in some cases it may not be possible to avoid expunging
 * messages that are marked deleted by another client at the same time
 * as the expunge_uids call is running.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_disco_folder_expunge_uids (CamelFolder *folder,
                                 GPtrArray *uids,
                                 GCancellable *cancellable,
                                 GError **error)
{
	g_return_val_if_fail (CAMEL_IS_DISCO_FOLDER (folder), FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	return disco_expunge_uids (folder, uids, cancellable, error);
}

/**
 * camel_disco_folder_cache_message:
 * @disco_folder: the folder
 * @uid: the UID of the message to cache
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Requests that @disco_folder cache message @uid to disk.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_disco_folder_cache_message (CamelDiscoFolder *disco_folder,
                                  const gchar *uid,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelDiscoFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_DISCO_FOLDER (disco_folder), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	class = CAMEL_DISCO_FOLDER_GET_CLASS (disco_folder);
	g_return_val_if_fail (class->cache_message != NULL, FALSE);

	success = class->cache_message (
		disco_folder, uid, cancellable, error);
	CAMEL_CHECK_GERROR (disco_folder, cache_message, success, error);

	return success;
}

/**
 * camel_disco_folder_prepare_for_offline:
 * @disco_folder: the folder
 * @expression: an expression describing messages to synchronize, or %NULL
 * if all messages should be sync'ed.
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * This prepares @disco_folder for offline operation, by downloading
 * the bodies of all messages described by @expression (using the
 * same syntax as camel_folder_search_by_expression() ).
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_disco_folder_prepare_for_offline (CamelDiscoFolder *disco_folder,
                                        const gchar *expression,
                                        GCancellable *cancellable,
                                        GError **error)
{
	CamelDiscoFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_DISCO_FOLDER (disco_folder), FALSE);

	class = CAMEL_DISCO_FOLDER_GET_CLASS (disco_folder);
	g_return_val_if_fail (class->prepare_for_offline != NULL, FALSE);

	success = class->prepare_for_offline (
		disco_folder, expression, cancellable, error);
	CAMEL_CHECK_GERROR (disco_folder, prepare_for_offline, success, error);

	return success;
}

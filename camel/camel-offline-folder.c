/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>

#include "camel-debug.h"
#include "camel-offline-folder.h"
#include "camel-operation.h"
#include "camel-service.h"
#include "camel-session.h"
#include "camel-store.h"

#define CAMEL_OFFLINE_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_OFFLINE_FOLDER, CamelOfflineFolderPrivate))

struct _CamelOfflineFolderPrivate {
	gboolean offline_sync;
};

struct _offline_downsync_msg {
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

G_DEFINE_TYPE (CamelOfflineFolder, camel_offline_folder, CAMEL_TYPE_FOLDER)

static void
offline_downsync_sync (CamelSession *session, CamelSessionThreadMsg *mm)
{
	struct _offline_downsync_msg *m = (struct _offline_downsync_msg *) mm;
	gint i;

	camel_operation_start (NULL, _("Downloading new messages for offline mode"));

	if (m->changes) {
		for (i = 0; i < m->changes->uid_added->len; i++) {
			gint pc = i * 100 / m->changes->uid_added->len;

			camel_operation_progress (NULL, pc);
			camel_folder_sync_message (m->folder, m->changes->uid_added->pdata[i], &mm->error);
		}
	} else {
		camel_offline_folder_downsync ((CamelOfflineFolder *) m->folder, "(match-all)", &mm->error);
	}

	camel_operation_end (NULL);
}

static void
offline_downsync_free (CamelSession *session, CamelSessionThreadMsg *mm)
{
	struct _offline_downsync_msg *m = (struct _offline_downsync_msg *) mm;

	if (m->changes)
		camel_folder_change_info_free (m->changes);

	g_object_unref (m->folder);
}

static CamelSessionThreadOps offline_downsync_ops = {
	offline_downsync_sync,
	offline_downsync_free,
};

static void
offline_folder_changed (CamelFolder *folder,
                        CamelFolderChangeInfo *changes)
{
	CamelStore *parent_store;
	CamelService *service;
	gboolean offline_sync;

	parent_store = camel_folder_get_parent_store (folder);
	service = CAMEL_SERVICE (parent_store);

	offline_sync = camel_offline_folder_get_offline_sync (
		CAMEL_OFFLINE_FOLDER (folder));

	if (changes->uid_added->len > 0 && (offline_sync || camel_url_get_param (service->url, "sync_offline"))) {
		CamelSession *session = service->session;
		struct _offline_downsync_msg *m;

		m = camel_session_thread_msg_new (session, &offline_downsync_ops, sizeof (*m));
		m->changes = camel_folder_change_info_new ();
		camel_folder_change_info_cat (m->changes, changes);
		m->folder = g_object_ref (folder);

		camel_session_thread_queue (session, &m->msg, 0);
	}
}

static void
offline_folder_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_OFFLINE_SYNC:
			camel_offline_folder_set_offline_sync (
				CAMEL_OFFLINE_FOLDER (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
offline_folder_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_OFFLINE_SYNC:
			g_value_set_boolean (
				value, camel_offline_folder_get_offline_sync (
				CAMEL_OFFLINE_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static gboolean
offline_folder_downsync (CamelOfflineFolder *offline,
                         const gchar *expression,
                         GError **error)
{
	CamelFolder *folder = (CamelFolder *) offline;
	GPtrArray *uids, *uncached_uids = NULL;
	gint i;

	camel_operation_start (
		NULL, _("Syncing messages in folder '%s' to disk"),
		camel_folder_get_full_name (folder));

	if (expression)
		uids = camel_folder_search_by_expression (folder, expression, NULL);
	else
		uids = camel_folder_get_uids (folder);

	if (!uids)
		goto done;
	uncached_uids = camel_folder_get_uncached_uids(folder, uids, NULL);
	if (uids) {
		if (expression)
			camel_folder_search_free (folder, uids);
		else
			camel_folder_free_uids (folder, uids);
	}

	if (!uncached_uids)
		goto done;

	for (i = 0; i < uncached_uids->len; i++) {
		gint pc = i * 100 / uncached_uids->len;
		camel_folder_sync_message (folder, uncached_uids->pdata[i], NULL);
		camel_operation_progress (NULL, pc);
	}

done:
	if (uncached_uids)
		camel_folder_free_uids(folder, uncached_uids);

	camel_operation_end (NULL);

	return TRUE;
}

static void
camel_offline_folder_class_init (CamelOfflineFolderClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelOfflineFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = offline_folder_set_property;
	object_class->get_property = offline_folder_get_property;

	class->downsync = offline_folder_downsync;

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
camel_offline_folder_init (CamelOfflineFolder *folder)
{
	folder->priv = CAMEL_OFFLINE_FOLDER_GET_PRIVATE (folder);

	g_signal_connect (
		folder, "changed",
		G_CALLBACK (offline_folder_changed), NULL);
}

/**
 * camel_offline_folder_get_offline_sync:
 * @offline_folder: a #CamelOfflineFolder
 *
 * Since: 2.32
 **/
gboolean
camel_offline_folder_get_offline_sync (CamelOfflineFolder *offline_folder)
{
	g_return_val_if_fail (CAMEL_IS_OFFLINE_FOLDER (offline_folder), FALSE);

	return offline_folder->priv->offline_sync;
}

/**
 * camel_offline_folder_set_offline_sync:
 * @offline_folder: a #CamelOfflineFolder
 * @offline_sync: whether to synchronize for offline use
 *
 * Since: 2.32
 **/
void
camel_offline_folder_set_offline_sync (CamelOfflineFolder *offline_folder,
                                       gboolean offline_sync)
{
	g_return_if_fail (CAMEL_IS_OFFLINE_FOLDER (offline_folder));

	offline_folder->priv->offline_sync = offline_sync;

	g_object_notify (G_OBJECT (offline_folder), "offline-sync");
}

/**
 * camel_offline_folder_downsync:
 * @offline: a #CamelOfflineFolder object
 * @expression: search expression describing which set of messages to downsync (%NULL for all)
 * @error: return location for a #GError, or %NULL
 *
 * Syncs messages in @offline described by the search @expression to
 * the local machine for offline availability.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_offline_folder_downsync (CamelOfflineFolder *offline,
                               const gchar *expression,
                               GError **error)
{
	CamelOfflineFolderClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_OFFLINE_FOLDER (offline), FALSE);

	class = CAMEL_OFFLINE_FOLDER_GET_CLASS (offline);
	g_return_val_if_fail (class->downsync != NULL, FALSE);

	success = class->downsync (offline, expression, error);
	CAMEL_CHECK_GERROR (offline, downsync, success, error);

	return success;
}

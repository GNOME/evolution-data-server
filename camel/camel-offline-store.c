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

#include "camel-folder.h"
#include "camel-offline-folder.h"
#include "camel-offline-settings.h"
#include "camel-offline-store.h"
#include "camel-session.h"

#define CAMEL_OFFLINE_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_OFFLINE_STORE, CamelOfflineStorePrivate))

struct _CamelOfflineStorePrivate {
	gboolean online;
};

G_DEFINE_TYPE (CamelOfflineStore, camel_offline_store, CAMEL_TYPE_STORE)

static void
offline_store_constructed (GObject *object)
{
	CamelOfflineStorePrivate *priv;
	CamelSession *session;

	priv = CAMEL_OFFLINE_STORE_GET_PRIVATE (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (camel_offline_store_parent_class)->
		constructed (object);

	session = camel_service_get_session (CAMEL_SERVICE (object));
	priv->online = camel_session_get_online (session);
}

static void
camel_offline_store_class_init (CamelOfflineStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;

	g_type_class_add_private (class, sizeof (CamelOfflineStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = offline_store_constructed;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_OFFLINE_SETTINGS;
}

static void
camel_offline_store_init (CamelOfflineStore *store)
{
	store->priv = CAMEL_OFFLINE_STORE_GET_PRIVATE (store);
}

/**
 * camel_offline_store_get_online:
 * @store: a #CamelOfflineStore
 *
 * Returns %TRUE if @store is online.
 *
 * Since: 2.24
 **/
gboolean
camel_offline_store_get_online (CamelOfflineStore *store)
{
	g_return_val_if_fail (CAMEL_IS_OFFLINE_STORE (store), 0);

	return store->priv->online;
}

/**
 * camel_offline_store_set_online_sync:
 * @store: a #CamelOfflineStore
 * @online: %TRUE for online, %FALSE for offline
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Sets the online/offline state of @store according to @online.
 **/
gboolean
camel_offline_store_set_online_sync (CamelOfflineStore *store,
                                     gboolean online,
                                     GCancellable *cancellable,
                                     GError **error)
{
	CamelService *service;
	CamelSession *session;
	CamelSettings *settings;
	gboolean network_available;
	gboolean store_is_online;
	gboolean sync_store;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_OFFLINE_STORE (store), FALSE);

	if (store->priv->online == online)
		return TRUE;

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);

	network_available = camel_session_get_network_available (session);
	store_is_online = camel_offline_store_get_online (store);

	settings = camel_service_ref_settings (service);

	sync_store = camel_offline_settings_get_stay_synchronized (
		CAMEL_OFFLINE_SETTINGS (settings));

	g_object_unref (settings);

	/* Returning to online mode is the simpler case. */
	if (!store_is_online) {
		store->priv->online = online;
		return camel_service_connect_sync (
			service, cancellable, error);
	}

	/* network available -> network unavailable */
	if (network_available) {
		GPtrArray *folders;
		guint ii;

		folders = camel_object_bag_list (
			CAMEL_STORE (store)->folders);

		for (ii = 0; ii < folders->len; ii++) {
			CamelFolder *folder = folders->pdata[ii];
			gboolean sync_folder;

			if (!CAMEL_IS_OFFLINE_FOLDER (folder))
				continue;

			sync_folder =
				camel_offline_folder_get_offline_sync (
				CAMEL_OFFLINE_FOLDER (folder));

			if (sync_store || sync_folder)
				camel_offline_folder_downsync_sync (
					CAMEL_OFFLINE_FOLDER (folder),
					NULL, cancellable, NULL);
		}

		g_ptr_array_foreach (folders, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (folders, TRUE);

		camel_store_synchronize_sync (
			CAMEL_STORE (store), FALSE, cancellable, NULL);
	}

	success = camel_service_disconnect_sync (
		service, network_available, cancellable, error);

	store->priv->online = online;

	return success;
}

/**
 * camel_offline_store_prepare_for_offline_sync:
 *
 * Since: 2.22
 **/
gboolean
camel_offline_store_prepare_for_offline_sync (CamelOfflineStore *store,
                                              GCancellable *cancellable,
                                              GError **error)
{
	CamelService *service;
	CamelSession *session;
	CamelSettings *settings;
	gboolean network_available;
	gboolean store_is_online;
	gboolean sync_store;

	g_return_val_if_fail (CAMEL_IS_OFFLINE_STORE (store), FALSE);

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);

	network_available = camel_session_get_network_available (session);
	store_is_online = camel_offline_store_get_online (store);

	settings = camel_service_ref_settings (service);

	sync_store = camel_offline_settings_get_stay_synchronized (
		CAMEL_OFFLINE_SETTINGS (settings));

	g_object_unref (settings);

	if (network_available && store_is_online) {
		GPtrArray *folders;
		guint ii;

		folders = camel_object_bag_list (
			CAMEL_STORE (store)->folders);

		for (ii = 0; ii < folders->len; ii++) {
			CamelFolder *folder = folders->pdata[ii];
			gboolean sync_folder;

			if (!CAMEL_IS_OFFLINE_FOLDER (folder))
				continue;

			sync_folder =
				camel_offline_folder_get_offline_sync (
				CAMEL_OFFLINE_FOLDER (folder));

			if (sync_store || sync_folder) {
				camel_offline_folder_downsync_sync (
					CAMEL_OFFLINE_FOLDER (folder),
					NULL, cancellable, NULL);
			}
		}

		g_ptr_array_foreach (folders, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (folders, TRUE);
	}

	if (network_available)
		camel_store_synchronize_sync (
			CAMEL_STORE (store), FALSE, cancellable, NULL);

	return TRUE;
}

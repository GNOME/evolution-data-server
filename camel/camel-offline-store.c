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
#include "camel-offline-store.h"
#include "camel-session.h"

struct _CamelOfflineStorePrivate {
	gboolean online;
};

G_DEFINE_TYPE (CamelOfflineStore, camel_offline_store, CAMEL_TYPE_STORE)

static gboolean
offline_store_construct (CamelService *service,
                         CamelSession *session,
                         CamelProvider *provider,
                         CamelURL *url,
                         GError **error)
{
	CamelOfflineStore *store = CAMEL_OFFLINE_STORE (service);
	CamelServiceClass *service_class;

	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_offline_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	store->priv->online = camel_session_get_online (session);

	return TRUE;
}

static void
camel_offline_store_class_init (CamelOfflineStoreClass *class)
{
	CamelServiceClass *service_class;

	g_type_class_add_private (class, sizeof (CamelOfflineStorePrivate));

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = offline_store_construct;
}

static void
camel_offline_store_init (CamelOfflineStore *store)
{
	store->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		store, CAMEL_TYPE_OFFLINE_STORE, CamelOfflineStorePrivate);
	store->priv->online = TRUE;
}

/**
 * camel_offline_store_get_online:
 * @store: a #CamelOfflineStore object
 * @error: return location for a #GError, or %NULL
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
 * @store: a #CamelOfflineStore object
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
	CamelService *service = CAMEL_SERVICE (store);
	CamelSession *session;
	gboolean network_available;

	g_return_val_if_fail (CAMEL_IS_OFFLINE_STORE (store), FALSE);

	if (store->priv->online == online)
		return TRUE;

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);
	network_available = camel_session_get_network_available (session);

	if (store->priv->online) {
		/* network available -> network unavailable */
		if (network_available) {
			if (((CamelStore *) store)->folders) {
				GPtrArray *folders;
				CamelFolder *folder;
				CamelURL *url;
				gint i, sync;

				url = camel_service_get_camel_url (service);
				sync = camel_url_get_param (url, "sync_offline") != NULL;

				folders = camel_object_bag_list (((CamelStore *) store)->folders);
				for (i = 0; i < folders->len; i++) {
					folder = folders->pdata[i];

					if (G_TYPE_CHECK_INSTANCE_TYPE (folder, CAMEL_TYPE_OFFLINE_FOLDER)
					    && (sync || camel_offline_folder_get_offline_sync (CAMEL_OFFLINE_FOLDER (folder)))) {
						camel_offline_folder_downsync_sync (
							(CamelOfflineFolder *) folder,
							NULL, cancellable, NULL);
					}

					g_object_unref (folder);
				}

				g_ptr_array_free (folders, TRUE);
			}

			camel_store_synchronize_sync (
				CAMEL_STORE (store),
				FALSE, cancellable, NULL);
		}

		if (!camel_service_disconnect_sync (
			CAMEL_SERVICE (store), network_available, error)) {
			store->priv->online = online;
			return FALSE;
		}
	} else {
		store->priv->online = online;
		/* network unavailable -> network available */
		if (!camel_service_connect_sync (CAMEL_SERVICE (store), error)) {
			return FALSE;
		}
	}

	store->priv->online = online;

	return TRUE;
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

	g_return_val_if_fail (CAMEL_IS_OFFLINE_STORE (store), FALSE);

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);

	if (camel_session_get_network_available (session)) {
		if (store->priv->online) {
			if (((CamelStore *) store)->folders) {
				GPtrArray *folders;
				CamelFolder *folder;
				CamelURL *url;
				gint i, sync;

				url = camel_service_get_camel_url (service);
				sync = camel_url_get_param (url, "sync_offline") != NULL;

				folders = camel_object_bag_list (((CamelStore *) store)->folders);
				for (i = 0; i < folders->len; i++) {
					folder = folders->pdata[i];

					if (G_TYPE_CHECK_INSTANCE_TYPE (folder, CAMEL_TYPE_OFFLINE_FOLDER)
					    && (sync || camel_offline_folder_get_offline_sync (CAMEL_OFFLINE_FOLDER (folder)))) {
						camel_offline_folder_downsync_sync ((CamelOfflineFolder *) folder, NULL, cancellable, NULL);
					}
					g_object_unref (folder);
				}
				g_ptr_array_free (folders, TRUE);
			}
		}

		camel_store_synchronize_sync (
			CAMEL_STORE (store),
			FALSE, cancellable, NULL);
	}

	return TRUE;
}

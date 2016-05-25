/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Jeffrey Stedfast <fejj@novell.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>

#include "camel-folder.h"
#include "camel-network-service.h"
#include "camel-offline-folder.h"
#include "camel-offline-settings.h"
#include "camel-offline-store.h"
#include "camel-session.h"

#define CAMEL_OFFLINE_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_OFFLINE_STORE, CamelOfflineStorePrivate))

struct _CamelOfflineStorePrivate {
	/* XXX The online flag stores whether the user has selected online or
	 *     offline mode, but fetching the flag through the "get" function
	 *     also takes into account CamelNetworkService's "host-reachable"
	 *     property.  So it's possible to set the "online" state to TRUE,
	 *     but then immediately read back FALSE.  Kinda weird, but mainly
	 *     for temporary backward-compability. */
	gboolean online;
};

enum {
	PROP_0,
	PROP_ONLINE
};

G_DEFINE_TYPE (CamelOfflineStore, camel_offline_store, CAMEL_TYPE_STORE)

static void
offline_store_constructed (GObject *object)
{
	CamelOfflineStorePrivate *priv;
	CamelSession *session;

	priv = CAMEL_OFFLINE_STORE_GET_PRIVATE (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (camel_offline_store_parent_class)->constructed (object);

	session = camel_service_ref_session (CAMEL_SERVICE (object));
	priv->online = session && camel_session_get_online (session);
	g_clear_object (&session);
}

static void
offline_store_get_property (GObject *object,
                            guint property_id,
                            GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ONLINE:
			g_value_set_boolean (
				value, camel_offline_store_get_online (
				CAMEL_OFFLINE_STORE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
offline_store_notify (GObject *object,
                      GParamSpec *pspec)
{
	if (g_strcmp0 (pspec->name, "host-reachable") == 0)
		g_object_notify (object, "online");

	/* Chain up to parent's notify() method. */
	G_OBJECT_CLASS (camel_offline_store_parent_class)->
		notify (object, pspec);
}

static void
camel_offline_store_class_init (CamelOfflineStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;

	g_type_class_add_private (class, sizeof (CamelOfflineStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->constructed = offline_store_constructed;
	object_class->get_property = offline_store_get_property;
	object_class->notify = offline_store_notify;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_OFFLINE_SETTINGS;

	g_object_class_install_property (
		object_class,
		PROP_ONLINE,
		g_param_spec_boolean (
			"online",
			"Online",
			"Whether the store is online",
			FALSE,
			G_PARAM_READABLE));
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

	if (CAMEL_IS_NETWORK_SERVICE (store)) {
		CamelNetworkService *service;

		service = CAMEL_NETWORK_SERVICE (store);

		/* Always return FALSE if the remote host is not reachable. */
		if (!camel_network_service_get_host_reachable (service))
			return FALSE;
	}

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
	CamelServiceConnectionStatus status;
	gboolean host_reachable = TRUE;
	gboolean store_is_online;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_OFFLINE_STORE (store), FALSE);

	if (camel_offline_store_get_online (store) == online)
		return TRUE;

	service = CAMEL_SERVICE (store);
	status = camel_service_get_connection_status (service);

	if (CAMEL_IS_NETWORK_SERVICE (store)) {
		/* When going to set the 'online' state, then check with up-to-date
		   value, otherwise use the cached value. The cached value is
		   updated with few seconds timeout, thus it can be stale here. */
		if (online)
			host_reachable =
				camel_network_service_can_reach_sync (
				CAMEL_NETWORK_SERVICE (store),
				cancellable, NULL);
		else
			host_reachable =
				camel_network_service_get_host_reachable (
				CAMEL_NETWORK_SERVICE (store));
	}

	store_is_online = camel_offline_store_get_online (store);

	/* Returning to online mode is the simpler case. */
	if (!store_is_online) {
		store->priv->online = online;

		g_object_notify (G_OBJECT (store), "online");

		if (status == CAMEL_SERVICE_CONNECTING)
			return TRUE;

		return camel_service_connect_sync (service, cancellable, error);
	}

	if (host_reachable) {
		CamelSession *session;

		session = camel_service_ref_session (service);
		host_reachable = session && camel_session_get_online (session);
		g_clear_object (&session);
	}

	if (host_reachable) {
		GPtrArray *folders;
		guint ii;

		folders = camel_object_bag_list (
			CAMEL_STORE (store)->folders);

		for (ii = 0; ii < folders->len; ii++) {
			CamelFolder *folder = folders->pdata[ii];
			CamelOfflineFolder *offline_folder;

			if (!CAMEL_IS_OFFLINE_FOLDER (folder))
				continue;

			offline_folder = CAMEL_OFFLINE_FOLDER (folder);

			if (camel_offline_folder_can_downsync (offline_folder))
				camel_offline_folder_downsync_sync (offline_folder, NULL, cancellable, NULL);
		}

		g_ptr_array_foreach (folders, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (folders, TRUE);

		camel_store_synchronize_sync (
			CAMEL_STORE (store), FALSE, cancellable, NULL);
	}

	if (status != CAMEL_SERVICE_DISCONNECTING) {
		success = camel_service_disconnect_sync (
			service, host_reachable, cancellable, error);
	}

	store->priv->online = online;

	g_object_notify (G_OBJECT (store), "online");

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
	gboolean host_reachable = TRUE;
	gboolean store_is_online;

	g_return_val_if_fail (CAMEL_IS_OFFLINE_STORE (store), FALSE);

	store_is_online = camel_offline_store_get_online (store);

	if (store_is_online && CAMEL_IS_NETWORK_SERVICE (store)) {
		/* Check with up-to-date value. The cached value is updated with
		   few seconds timeout, thus it can be stale here. */
		host_reachable =
			camel_network_service_can_reach_sync (
			CAMEL_NETWORK_SERVICE (store),
			cancellable, NULL);
	}

	if (host_reachable && store_is_online) {
		GPtrArray *folders;
		guint ii;

		folders = camel_object_bag_list (
			CAMEL_STORE (store)->folders);

		for (ii = 0; ii < folders->len; ii++) {
			CamelFolder *folder = folders->pdata[ii];
			CamelOfflineFolder *offline_folder;

			if (!CAMEL_IS_OFFLINE_FOLDER (folder))
				continue;

			offline_folder = CAMEL_OFFLINE_FOLDER (folder);

			if (camel_offline_folder_can_downsync (offline_folder))
				camel_offline_folder_downsync_sync (offline_folder, NULL, cancellable, NULL);
		}

		g_ptr_array_foreach (folders, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (folders, TRUE);
	}

	if (host_reachable)
		camel_store_synchronize_sync (
			CAMEL_STORE (store), FALSE, cancellable, NULL);

	return TRUE;
}

/**
 * camel_offline_store_requires_downsync:
 * @store: a #CamelOfflineStore
 *
 * Check whether the @store requires synchronization for offline usage.
 * This is not blocking, it only checks settings on the store and its
 * currently opened folders.
 *
 * Returns %TRUE if the @store requires synchronization for offline usage
 *
 * Since: 3.12
 **/
gboolean
camel_offline_store_requires_downsync (CamelOfflineStore *store)
{
	gboolean host_reachable = TRUE;
	gboolean store_is_online;
	gboolean sync_any_folder = FALSE;

	g_return_val_if_fail (CAMEL_IS_OFFLINE_STORE (store), FALSE);

	if (CAMEL_IS_NETWORK_SERVICE (store)) {
		host_reachable =
			camel_network_service_get_host_reachable (
			CAMEL_NETWORK_SERVICE (store));
	}

	store_is_online = camel_offline_store_get_online (store);

	if (!store_is_online)
		return FALSE;

	if (host_reachable) {
		CamelSession *session;

		session = camel_service_ref_session (CAMEL_SERVICE (store));
		host_reachable = session && camel_session_get_online (session);
		g_clear_object (&session);
	}

	if (host_reachable) {
		GPtrArray *folders;
		guint ii;

		folders = camel_object_bag_list (
			CAMEL_STORE (store)->folders);

		for (ii = 0; ii < folders->len && !sync_any_folder; ii++) {
			CamelFolder *folder = folders->pdata[ii];

			if (!CAMEL_IS_OFFLINE_FOLDER (folder))
				continue;

			sync_any_folder = camel_offline_folder_can_downsync (CAMEL_OFFLINE_FOLDER (folder));
		}

		g_ptr_array_foreach (folders, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (folders, TRUE);
	}

	return sync_any_folder && host_reachable;
}

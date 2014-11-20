/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-store.c : class for a imap store */
/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-imapx-conn-manager.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-job.h"
#include "camel-imapx-server.h"
#include "camel-imapx-settings.h"
#include "camel-imapx-store.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-utils.h"

/* Specified in RFC 2060 section 2.1 */
#define IMAP_PORT 143
#define IMAPS_PORT 993

#define FINFO_REFRESH_INTERVAL 60

#define CAMEL_IMAPX_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_STORE, CamelIMAPXStorePrivate))

#define e(...) camel_imapx_debug(extra, __VA_ARGS__)

struct _CamelIMAPXStorePrivate {
	CamelIMAPXConnManager *con_man;

	CamelIMAPXServer *connecting_server;
	gboolean is_concurrent_connection;

	GMutex server_lock;

	GHashTable *quota_info;
	GMutex quota_info_lock;

	GMutex settings_lock;
	CamelSettings *settings;
	gulong settings_notify_handler_id;

	/* Used for synchronizing get_folder_info_sync(). */
	GMutex get_finfo_lock;
	time_t last_refresh_time;
	volatile gint syncing_folders;

	CamelIMAPXNamespaceResponse *namespaces;
	GMutex namespaces_lock;

	GHashTable *mailboxes;
	GMutex mailboxes_lock;
};

enum {
	PROP_0,
	PROP_CONNECTABLE,
	PROP_HOST_REACHABLE
};

enum {
	MAILBOX_CREATED,
	MAILBOX_RENAMED,
	MAILBOX_UPDATED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static GInitableIface *parent_initable_interface;

/* Forward Declarations */
static void camel_imapx_store_initable_init (GInitableIface *iface);
static void camel_network_service_init (CamelNetworkServiceInterface *iface);
static void camel_subscribable_init (CamelSubscribableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (
	CamelIMAPXStore,
	camel_imapx_store,
	CAMEL_TYPE_OFFLINE_STORE,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		camel_imapx_store_initable_init)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SERVICE,
		camel_network_service_init)
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_SUBSCRIBABLE,
		camel_subscribable_init))

static guint
imapx_name_hash (gconstpointer key)
{
	const gchar *mailbox = key;

	if (camel_imapx_mailbox_is_inbox (mailbox))
		mailbox = "INBOX";

	return g_str_hash (mailbox);
}

static gboolean
imapx_name_equal (gconstpointer a,
                  gconstpointer b)
{
	const gchar *mailbox_a = a;
	const gchar *mailbox_b = b;

	if (camel_imapx_mailbox_is_inbox (mailbox_a))
		mailbox_a = "INBOX";

	if (camel_imapx_mailbox_is_inbox (mailbox_b))
		mailbox_b = "INBOX";

	return g_str_equal (mailbox_a, mailbox_b);
}

static void
imapx_store_update_store_flags (CamelStore *store)
{
	CamelService *service;
	CamelSettings *settings;
	CamelIMAPXSettings *imapx_settings;

	service = CAMEL_SERVICE (store);
	settings = camel_service_ref_settings (service);
	imapx_settings = CAMEL_IMAPX_SETTINGS (settings);

	if (camel_imapx_settings_get_use_real_junk_path (imapx_settings)) {
		store->flags &= ~CAMEL_STORE_VJUNK;
		store->flags |= CAMEL_STORE_REAL_JUNK_FOLDER;
	} else {
		store->flags |= CAMEL_STORE_VJUNK;
		store->flags &= ~CAMEL_STORE_REAL_JUNK_FOLDER;
	}

	if (camel_imapx_settings_get_use_real_trash_path (imapx_settings))
		store->flags &= ~CAMEL_STORE_VTRASH;
	else
		store->flags |= CAMEL_STORE_VTRASH;

	g_object_unref (settings);
}

static void
imapx_store_settings_notify_cb (CamelSettings *settings,
                                GParamSpec *pspec,
                                CamelStore *store)
{
	gboolean folder_info_stale = g_str_equal (pspec->name, "use-subscriptions");

	if (g_str_equal (pspec->name, "use-real-junk-path") ||
	    g_str_equal (pspec->name, "use-real-trash-path") ||
	    g_str_equal (pspec->name, "real-junk-path") ||
	    g_str_equal (pspec->name, "real-trash-path")) {
		imapx_store_update_store_flags (store);
		folder_info_stale = TRUE;
	}

	if (folder_info_stale)
		camel_store_folder_info_stale (store);
}

static CamelFolderInfo *
imapx_store_build_folder_info (CamelIMAPXStore *imapx_store,
                               const gchar *folder_path,
                               CamelFolderInfoFlags flags)
{
	CamelStore *store = (CamelStore *) imapx_store;
	CamelSettings *settings;
	CamelFolderInfo *fi;
	const gchar *name;

	store = CAMEL_STORE (imapx_store);
	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (folder_path);
	fi->flags = flags;
	fi->unread = -1;
	fi->total = -1;

	name = strrchr (fi->full_name, '/');
	if (name == NULL)
		name = fi->full_name;
	else
		name++;

	if (camel_imapx_mailbox_is_inbox (fi->full_name)) {
		fi->display_name = g_strdup (_("Inbox"));
		fi->flags |= CAMEL_FOLDER_SYSTEM;
		fi->flags |= CAMEL_FOLDER_TYPE_INBOX;
	} else {
		fi->display_name = g_strdup (name);
	}

	if ((store->flags & CAMEL_STORE_VTRASH) == 0) {
		const gchar *trash_path;

		trash_path = camel_imapx_settings_get_real_trash_path (
			CAMEL_IMAPX_SETTINGS (settings));
		if (g_strcmp0 (trash_path, folder_path) == 0)
			fi->flags |= CAMEL_FOLDER_TYPE_TRASH;
	}

	if ((store->flags & CAMEL_STORE_REAL_JUNK_FOLDER) != 0) {
		const gchar *junk_path;

		junk_path = camel_imapx_settings_get_real_junk_path (
			CAMEL_IMAPX_SETTINGS (settings));
		if (g_strcmp0 (junk_path, folder_path) == 0)
			fi->flags |= CAMEL_FOLDER_TYPE_JUNK;
	}

	g_object_unref (settings);

	return fi;
}

static void
imapx_store_rename_folder_info (CamelIMAPXStore *imapx_store,
                                const gchar *old_folder_path,
                                const gchar *new_folder_path)
{
	GPtrArray *array;
	gint olen = strlen (old_folder_path);
	guint ii;

	array = camel_store_summary_array (imapx_store->summary);

	for (ii = 0; ii < array->len; ii++) {
		CamelStoreInfo *si;
		CamelIMAPXStoreInfo *imapx_si;
		const gchar *path;
		gchar *new_path;
		gchar *new_mailbox_name;

		si = g_ptr_array_index (array, ii);
		path = camel_store_info_path (imapx_store->summary, si);

		/* We need to adjust not only the entry for the renamed
		 * folder, but also the entries for all the descendants
		 * of the renamed folder. */

		if (!g_str_has_prefix (path, old_folder_path))
			continue;

		if (strlen (path) > olen)
			new_path = g_strdup_printf (
				"%s/%s", new_folder_path, path + olen + 1);
		else
			new_path = g_strdup (new_folder_path);

		camel_store_info_set_string (
			imapx_store->summary, si,
			CAMEL_STORE_INFO_PATH, new_path);

		imapx_si = (CamelIMAPXStoreInfo *) si;
		g_warn_if_fail (imapx_si->separator != '\0');

		new_mailbox_name =
			camel_imapx_folder_path_to_mailbox (
			new_path, imapx_si->separator);

		/* Takes ownership of new_mailbox_name. */
		g_free (imapx_si->mailbox_name);
		imapx_si->mailbox_name = new_mailbox_name;

		camel_store_summary_touch (imapx_store->summary);

		g_free (new_path);
	}

	camel_store_summary_array_free (imapx_store->summary, array);
}

static void
imapx_store_rename_storage_path (CamelIMAPXStore *imapx_store,
                                 const gchar *old_mailbox,
                                 const gchar *new_mailbox)
{
	CamelService *service;
	const gchar *user_cache_dir;
	gchar *root_storage_path;
	gchar *old_storage_path;
	gchar *new_storage_path;

	service = CAMEL_SERVICE (imapx_store);
	user_cache_dir = camel_service_get_user_cache_dir (service);
	root_storage_path = g_build_filename (user_cache_dir, "folders", NULL);

	old_storage_path =
		imapx_path_to_physical (root_storage_path, old_mailbox);
	new_storage_path =
		imapx_path_to_physical (root_storage_path, new_mailbox);

	if (g_rename (old_storage_path, new_storage_path) == -1 && errno != ENOENT) {
		g_warning (
			"Could not rename message cache "
			"'%s' to '%s: %s: cache reset",
			old_storage_path,
			new_storage_path,
			g_strerror (errno));
	}

	g_free (root_storage_path);
	g_free (old_storage_path);
	g_free (new_storage_path);
}

static void
imapx_store_add_mailbox_to_folder (CamelIMAPXStore *store,
                                   CamelIMAPXMailbox *mailbox)
{
	CamelIMAPXFolder *folder;
	gchar *folder_path;

	/* Add the CamelIMAPXMailbox to a cached CamelIMAPXFolder. */

	folder_path = camel_imapx_mailbox_dup_folder_path (mailbox);

	folder = camel_object_bag_get (
		CAMEL_STORE (store)->folders, folder_path);

	if (folder != NULL) {
		camel_imapx_folder_set_mailbox (folder, mailbox);
		g_object_unref (folder);
	}

	g_free (folder_path);
}

static CamelStoreInfoFlags
imapx_store_mailbox_attributes_to_flags (CamelIMAPXMailbox *mailbox)
{
	CamelStoreInfoFlags store_info_flags = 0;
	const gchar *attribute;

	attribute = CAMEL_IMAPX_LIST_ATTR_NOSELECT;
	if (camel_imapx_mailbox_has_attribute (mailbox, attribute) &&
	    !camel_imapx_mailbox_is_inbox (camel_imapx_mailbox_get_name (mailbox)))
		store_info_flags |= CAMEL_STORE_INFO_FOLDER_NOSELECT;

	attribute = CAMEL_IMAPX_LIST_ATTR_NOINFERIORS;
	if (camel_imapx_mailbox_has_attribute (mailbox, attribute))
		store_info_flags |= CAMEL_STORE_INFO_FOLDER_NOINFERIORS;

	attribute = CAMEL_IMAPX_LIST_ATTR_HASCHILDREN;
	if (camel_imapx_mailbox_has_attribute (mailbox, attribute))
		store_info_flags |= CAMEL_STORE_INFO_FOLDER_CHILDREN;

	attribute = CAMEL_IMAPX_LIST_ATTR_HASNOCHILDREN;
	if (camel_imapx_mailbox_has_attribute (mailbox, attribute))
		store_info_flags |= CAMEL_STORE_INFO_FOLDER_NOCHILDREN;

	attribute = CAMEL_IMAPX_LIST_ATTR_SUBSCRIBED;
	if (camel_imapx_mailbox_has_attribute (mailbox, attribute))
		store_info_flags |= CAMEL_STORE_INFO_FOLDER_SUBSCRIBED;

	/* XXX Does "\Marked" mean CAMEL_STORE_INFO_FOLDER_FLAGGED?
	 *     Who the heck knows; the enum value is undocumented. */

	return store_info_flags;
}

static void
imapx_store_process_mailbox_attributes (CamelIMAPXStore *store,
                                        CamelIMAPXMailbox *mailbox,
                                        const gchar *oldname)
{
	CamelFolderInfo *fi;
	CamelIMAPXStoreInfo *si;
	CamelStoreInfoFlags flags;
	CamelSettings *settings;
	gboolean use_subscriptions;
	gboolean mailbox_is_subscribed;
	gboolean mailbox_is_nonexistent;
	gboolean mailbox_was_in_summary;
	gboolean mailbox_was_subscribed;
	gboolean emit_folder_created_subscribed = FALSE;
	gboolean emit_folder_unsubscribed_deleted = FALSE;
	gboolean emit_folder_renamed = FALSE;
	gchar *folder_path;
	const gchar *mailbox_name;
	gchar separator;

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));
	use_subscriptions = camel_imapx_settings_get_use_subscriptions (
		CAMEL_IMAPX_SETTINGS (settings));
	g_object_unref (settings);

	mailbox_name = camel_imapx_mailbox_get_name (mailbox);
	separator = camel_imapx_mailbox_get_separator (mailbox);

	mailbox_is_subscribed =
		camel_imapx_mailbox_has_attribute (
		mailbox, CAMEL_IMAPX_LIST_ATTR_SUBSCRIBED) ||
		camel_imapx_mailbox_is_inbox (mailbox_name);

	mailbox_is_nonexistent =
		camel_imapx_mailbox_has_attribute (
		mailbox, CAMEL_IMAPX_LIST_ATTR_NONEXISTENT);

	/* XXX The flags type transforms from CamelStoreInfoFlags
	 *     to CamelFolderInfoFlags about half-way through this.
	 *     We should really eliminate the confusing redundancy. */
	flags = imapx_store_mailbox_attributes_to_flags (mailbox);

	/* Summary retains ownership of the returned CamelStoreInfo. */
	si = camel_imapx_store_summary_mailbox (store->summary, mailbox_name);
	if (!si && oldname)
		si = camel_imapx_store_summary_mailbox (store->summary, oldname);
	if (si != NULL) {
		mailbox_was_in_summary = TRUE;
		if (si->info.flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)
			mailbox_was_subscribed = TRUE;
		else
			mailbox_was_subscribed = FALSE;
	} else {
		/* XXX Shouldn't this take a GError if it can fail? */
		si = camel_imapx_store_summary_add_from_mailbox (
			store->summary, mailbox);
		g_return_if_fail (si != NULL);
		mailbox_was_in_summary = FALSE;
		mailbox_was_subscribed = FALSE;
	}

	/* Check whether the flags disagree. */
	if (si->info.flags != flags) {
		si->info.flags = flags;
		camel_store_summary_touch (store->summary);
	}

	folder_path = camel_imapx_mailbox_to_folder_path (mailbox_name, separator);
	fi = imapx_store_build_folder_info (store, folder_path, flags);

	/* Figure out which signals to emit, if any. */
	if (use_subscriptions) {
		/* If we are honoring folder subscriptions, then
		 * subscription changes are equivalent to folder
		 * creation / deletion as far as we're concerned. */
		if (mailbox_is_subscribed && !mailbox_is_nonexistent) {
			if (oldname != NULL) {
				emit_folder_renamed = TRUE;
			} else if (!mailbox_was_subscribed) {
				emit_folder_created_subscribed = TRUE;
			}
		}
		if (!mailbox_is_subscribed && mailbox_was_subscribed)
			emit_folder_unsubscribed_deleted = TRUE;
		if (mailbox_is_nonexistent && mailbox_was_subscribed)
			emit_folder_unsubscribed_deleted = TRUE;
	} else {
		if (!mailbox_is_nonexistent) {
			if (oldname != NULL) {
				emit_folder_renamed = TRUE;
			} else if (!mailbox_was_in_summary) {
				emit_folder_created_subscribed = TRUE;
			}
		}
		if (mailbox_is_nonexistent && mailbox_was_in_summary)
			emit_folder_unsubscribed_deleted = TRUE;
	}

	/* Suppress all signal emissions when synchronizing folders. */
	if (g_atomic_int_get (&store->priv->syncing_folders) > 0) {
		emit_folder_created_subscribed = FALSE;
		emit_folder_unsubscribed_deleted = FALSE;
		emit_folder_renamed = FALSE;
	} else {
		/* At most one signal emission flag should be set. */
		g_warn_if_fail (
			(emit_folder_created_subscribed ? 1 : 0) +
			(emit_folder_unsubscribed_deleted ? 1 : 0) +
			(emit_folder_renamed ? 1 : 0) <= 1);
	}

	if (emit_folder_created_subscribed) {
		camel_store_folder_created (
			CAMEL_STORE (store), fi);
		camel_subscribable_folder_subscribed (
			CAMEL_SUBSCRIBABLE (store), fi);
	}

	if (emit_folder_unsubscribed_deleted) {
		camel_subscribable_folder_unsubscribed (
			CAMEL_SUBSCRIBABLE (store), fi);
		camel_store_folder_deleted (
			CAMEL_STORE (store), fi);
	}

	if (emit_folder_renamed) {
		gchar *old_folder_path;
		gchar *new_folder_path;

		old_folder_path = camel_imapx_mailbox_to_folder_path (
			oldname, separator);
		new_folder_path = camel_imapx_mailbox_to_folder_path (
			mailbox_name, separator);

		imapx_store_rename_folder_info (
			store, old_folder_path, new_folder_path);
		imapx_store_rename_storage_path (
			store, old_folder_path, new_folder_path);

		camel_store_folder_renamed (CAMEL_STORE (store), old_folder_path, fi);

		g_free (old_folder_path);
		g_free (new_folder_path);
	}

	camel_folder_info_free (fi);
	g_free (folder_path);
}

static void
imapx_store_process_mailbox_status (CamelIMAPXStore *imapx_store,
                                    CamelIMAPXMailbox *mailbox)
{
	CamelStore *store;
	CamelFolder *folder;
	gchar *folder_path;

	folder_path = camel_imapx_mailbox_dup_folder_path (mailbox);
	store = CAMEL_STORE (imapx_store);

	/* Update only already opened folders */
	folder = camel_object_bag_reserve (store->folders, folder_path);
	if (folder != NULL) {
		CamelIMAPXFolder *imapx_folder;
		CamelIMAPXSummary *imapx_summary;
		guint32 uidvalidity;

		imapx_folder = CAMEL_IMAPX_FOLDER (folder);
		imapx_summary = CAMEL_IMAPX_SUMMARY (folder->summary);

		uidvalidity = camel_imapx_mailbox_get_uidvalidity (mailbox);

		if (uidvalidity > 0 && uidvalidity != imapx_summary->validity)
			camel_imapx_folder_invalidate_local_cache (
				imapx_folder, uidvalidity);

		g_object_unref (folder);
	} else {
		camel_object_bag_abort (store->folders, folder_path);
	}

	g_free (folder_path);
}

static void
imapx_store_connect_to_settings (CamelStore *store)
{
	CamelIMAPXStorePrivate *priv;
	CamelSettings *settings;
	gulong handler_id;

	/* XXX I considered calling camel_store_folder_info_stale()
	 *     here, but I suspect it would create unnecessary extra
	 *     work for applications during startup since the signal
	 *     is not emitted immediately.
	 *
	 *     Let's just say whomever replaces the settings object
	 *     in a CamelService is reponsible for deciding whether
	 *     camel_store_folder_info_stale() should be called. */

	priv = CAMEL_IMAPX_STORE_GET_PRIVATE (store);

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	g_mutex_lock (&priv->settings_lock);

	if (priv->settings != NULL) {
		g_signal_handler_disconnect (
			priv->settings,
			priv->settings_notify_handler_id);
		priv->settings_notify_handler_id = 0;
		g_clear_object (&priv->settings);
	}

	priv->settings = g_object_ref (settings);

	handler_id = g_signal_connect (
		settings, "notify",
		G_CALLBACK (imapx_store_settings_notify_cb), store);
	priv->settings_notify_handler_id = handler_id;

	g_mutex_unlock (&priv->settings_lock);

	g_object_unref (settings);
}

static void
imapx_store_set_property (GObject *object,
                          guint property_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTABLE:
			camel_network_service_set_connectable (
				CAMEL_NETWORK_SERVICE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_store_get_property (GObject *object,
                          guint property_id,
                          GValue *value,
                          GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTABLE:
			g_value_take_object (
				value,
				camel_network_service_ref_connectable (
				CAMEL_NETWORK_SERVICE (object)));
			return;

		case PROP_HOST_REACHABLE:
			g_value_set_boolean (
				value,
				camel_network_service_get_host_reachable (
				CAMEL_NETWORK_SERVICE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_store_dispose (GObject *object)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (object);

	/* Force disconnect so we don't have it run later,
	 * after we've cleaned up some stuff. */
	if (imapx_store->priv->con_man != NULL) {
		camel_service_disconnect_sync (CAMEL_SERVICE (imapx_store), FALSE, NULL, NULL);
		g_clear_object (&imapx_store->priv->con_man);
	}

	if (imapx_store->priv->settings_notify_handler_id > 0) {
		g_signal_handler_disconnect (
			imapx_store->priv->settings,
			imapx_store->priv->settings_notify_handler_id);
		imapx_store->priv->settings_notify_handler_id = 0;
	}

	g_clear_object (&imapx_store->summary);

	g_clear_object (&imapx_store->priv->connecting_server);
	g_clear_object (&imapx_store->priv->settings);
	g_clear_object (&imapx_store->priv->namespaces);

	g_hash_table_remove_all (imapx_store->priv->mailboxes);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_store_parent_class)->dispose (object);
}

static void
imapx_store_finalize (GObject *object)
{
	CamelIMAPXStorePrivate *priv;

	priv = CAMEL_IMAPX_STORE_GET_PRIVATE (object);

	g_mutex_clear (&priv->get_finfo_lock);

	g_mutex_clear (&priv->server_lock);

	g_hash_table_destroy (priv->quota_info);
	g_mutex_clear (&priv->quota_info_lock);

	g_mutex_clear (&priv->settings_lock);

	g_mutex_clear (&priv->namespaces_lock);

	g_hash_table_destroy (priv->mailboxes);
	g_mutex_clear (&priv->mailboxes_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_store_parent_class)->finalize (object);
}

static void
imapx_store_notify (GObject *object,
                    GParamSpec *pspec)
{
	if (g_str_equal (pspec->name, "settings")) {
		imapx_store_connect_to_settings (CAMEL_STORE (object));
		imapx_store_update_store_flags (CAMEL_STORE (object));
	}

	/* Chain up to parent's notify() method. */
	G_OBJECT_CLASS (camel_imapx_store_parent_class)->
		notify (object, pspec);
}

static gchar *
imapx_get_name (CamelService *service,
                gboolean brief)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	gchar *host;
	gchar *user;
	gchar *name;

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	if (brief)
		name = g_strdup_printf (
			_("IMAP server %s"), host);
	else
		name = g_strdup_printf (
			_("IMAP service for %s on %s"), user, host);

	g_free (host);
	g_free (user);

	return name;
}

static gboolean
imapx_connect_sync (CamelService *service,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	gboolean success;

	imapx_store = CAMEL_IMAPX_STORE (service);

	imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, error);
	success = imapx_server != NULL;

	g_clear_object (&imapx_server);

	return success;
}

static gboolean
imapx_disconnect_sync (CamelService *service,
                       gboolean clean,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelIMAPXStorePrivate *priv;

	priv = CAMEL_IMAPX_STORE_GET_PRIVATE (service);

	if (priv->con_man != NULL)
		camel_imapx_conn_manager_close_connections (priv->con_man, NULL);

	g_mutex_lock (&priv->server_lock);

	g_clear_object (&priv->connecting_server);

	g_mutex_unlock (&priv->server_lock);

	return TRUE;
}

static CamelAuthenticationResult
imapx_authenticate_sync (CamelService *service,
                         const gchar *mechanism,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelIMAPXStorePrivate *priv;
	CamelIMAPXServer *imapx_server;
	CamelAuthenticationResult result;

	priv = CAMEL_IMAPX_STORE_GET_PRIVATE (service);

	/* This should have been set for us by connect_sync(). */
	g_mutex_lock (&priv->server_lock);
	imapx_server = g_object_ref (priv->connecting_server);
	g_mutex_unlock (&priv->server_lock);

	result = camel_imapx_server_authenticate (
		imapx_server, mechanism, cancellable, error);

	g_clear_object (&imapx_server);

	return result;
}

CamelServiceAuthType camel_imapx_password_authtype = {
	N_("Password"),

	N_("This option will connect to the IMAP server using a "
	   "plaintext password."),

	"",
	TRUE
};

static GList *
imapx_query_auth_types_sync (CamelService *service,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelServiceAuthType *authtype;
	CamelIMAPXStore *imapx_store;
	GList *sasl_types = NULL;
	GList *t, *next;
	CamelIMAPXServer *server;

	imapx_store = CAMEL_IMAPX_STORE (service);

	server = camel_imapx_server_new (imapx_store);
	server->tagprefix = 'Z';

	if (!imapx_connect_to_server (server, cancellable, error))
		goto exit;

	sasl_types = camel_sasl_authtype_list (FALSE);
	for (t = sasl_types; t; t = next) {
		authtype = t->data;
		next = t->next;

		if (!server->cinfo || !g_hash_table_lookup (server->cinfo->auth_types, authtype->authproto)) {
			sasl_types = g_list_remove_link (sasl_types, t);
			g_list_free_1 (t);
		}
	}

	sasl_types = g_list_prepend (
		sasl_types, &camel_imapx_password_authtype);

exit:
	g_object_unref (server);

	return sasl_types;
}

static CamelFolder *
get_folder_offline (CamelStore *store,
                    const gchar *folder_name,
                    CamelStoreGetFolderFlags flags,
                    GError **error)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (store);
	CamelFolder *new_folder = NULL;
	CamelStoreInfo *si;
	CamelService *service;
	const gchar *user_cache_dir;

	service = CAMEL_SERVICE (store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	si = camel_store_summary_path (imapx_store->summary, folder_name);

	if (si != NULL) {
		gchar *base_dir;
		gchar *folder_dir;

		base_dir = g_build_filename (user_cache_dir, "folders", NULL);
		folder_dir = imapx_path_to_physical (base_dir, folder_name);
		new_folder = camel_imapx_folder_new (
			store, folder_dir, folder_name, error);
		g_free (folder_dir);
		g_free (base_dir);

		camel_store_summary_info_unref (imapx_store->summary, si);
	} else {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder %s"), folder_name);
	}

	return new_folder;
}

static void
fill_fi (CamelStore *store,
         CamelFolderInfo *fi)
{
	CamelFolder *folder;

	folder = camel_object_bag_peek (store->folders, fi->full_name);
	if (folder) {
		CamelIMAPXFolder *imapx_folder;
		CamelIMAPXSummary *ims;
		CamelIMAPXMailbox *mailbox;

		if (folder->summary)
			ims = (CamelIMAPXSummary *) folder->summary;
		else
			ims = (CamelIMAPXSummary *) camel_imapx_summary_new (folder);

		imapx_folder = CAMEL_IMAPX_FOLDER (folder);
		mailbox = camel_imapx_folder_ref_mailbox (imapx_folder);

		fi->unread = camel_folder_summary_get_unread_count ((CamelFolderSummary *) ims);
		fi->total = camel_folder_summary_get_saved_count ((CamelFolderSummary *) ims);

		g_clear_object (&mailbox);

		if (!folder->summary)
			g_object_unref (ims);
		g_object_unref (folder);
	}
}

static void
imapx_delete_folder_from_cache (CamelIMAPXStore *imapx_store,
                                const gchar *folder_path)
{
	gchar *state_file;
	gchar *folder_dir, *storage_path;
	CamelFolderInfo *fi;
	CamelService *service;
	const gchar *user_cache_dir;

	service = CAMEL_SERVICE (imapx_store);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	storage_path = g_build_filename (user_cache_dir, "folders", NULL);
	folder_dir = imapx_path_to_physical (storage_path, folder_path);
	g_free (storage_path);
	if (g_access (folder_dir, F_OK) != 0) {
		g_free (folder_dir);
		goto event;
	}

	/* Delete summary and all the data */
	state_file = g_build_filename (folder_dir, "cmeta", NULL);
	g_unlink (state_file);
	g_free (state_file);

	camel_db_delete_folder (
		CAMEL_STORE (imapx_store)->cdb_w, folder_path, NULL);
	g_rmdir (folder_dir);

	state_file = g_build_filename (folder_dir, "subfolders", NULL);
	g_rmdir (state_file);
	g_free (state_file);

	g_rmdir (folder_dir);
	g_free (folder_dir);

event:
	camel_store_summary_remove_path (imapx_store->summary, folder_path);
	camel_store_summary_save (imapx_store->summary);

	fi = imapx_store_build_folder_info (imapx_store, folder_path, 0);
	camel_subscribable_folder_unsubscribed (CAMEL_SUBSCRIBABLE (imapx_store), fi);
	camel_store_folder_deleted (CAMEL_STORE (imapx_store), fi);
	camel_folder_info_free (fi);
}

static CamelFolderInfo *
get_folder_info_offline (CamelStore *store,
                         const gchar *top,
                         CamelStoreGetFolderInfoFlags flags,
                         GError **error)
{
	CamelIMAPXStore *imapx_store = CAMEL_IMAPX_STORE (store);
	CamelService *service;
	CamelSettings *settings;
	gboolean include_inbox = FALSE;
	CamelFolderInfo *fi;
	GPtrArray *folders;
	GPtrArray *array;
	gboolean use_subscriptions;
	guint ii;

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	use_subscriptions = camel_imapx_settings_get_use_subscriptions (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	/* FIXME: obey other flags */

	folders = g_ptr_array_new ();

	if (top == NULL || top[0] == '\0') {
		include_inbox = TRUE;
		top = "";
	}

	/* folder_info_build will insert parent nodes as necessary and mark
	 * them as noselect, which is information we actually don't have at
	 * the moment. So let it do the right thing by bailing out if it's
	 * not a folder we're explicitly interested in. */

	array = camel_store_summary_array (imapx_store->summary);

	for (ii = 0; ii < array->len; ii++) {
		CamelStoreInfo *si;
		const gchar *folder_path;
		gboolean si_is_inbox;
		gboolean si_is_match;

		si = g_ptr_array_index (array, ii);
		folder_path = camel_store_info_path (imapx_store->summary, si);
		si_is_inbox = (g_ascii_strcasecmp (folder_path, "INBOX") == 0);

		/* Filter by folder path. */
		si_is_match =
			(include_inbox && si_is_inbox) ||
			g_str_has_prefix (folder_path, top);

		if (!si_is_match)
			continue;

		/* Filter by subscription flags.
		 *
		 * Skip the folder if:
		 *   The user only wants to see subscribed folders
		 *   AND the folder is not subscribed
		 *   AND the caller only wants SUBSCRIBED folder info
		 *   AND the caller does NOT want a SUBSCRIPTION_LIST
		 *
		 * Note that having both SUBSCRIBED and SUBSCRIPTION_LIST
		 * flags set is contradictory.  SUBSCRIPTION_LIST wins in
		 * that case.
		 */
		si_is_match =
			!use_subscriptions ||
			(si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED) ||
			!(flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) ||
			(flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST);

		if (!si_is_match)
			continue;

		fi = imapx_store_build_folder_info (
			imapx_store, folder_path, 0);
		fi->unread = si->unread;
		fi->total = si->total;
		if ((fi->flags & CAMEL_FOLDER_TYPE_MASK) != 0)
			fi->flags =
				(fi->flags & CAMEL_FOLDER_TYPE_MASK) |
				(si->flags & ~CAMEL_FOLDER_TYPE_MASK);
		else
			fi->flags = si->flags;

		/* blah, this gets lost somewhere, i can't be bothered finding out why */
		if (si_is_inbox) {
			fi->flags =
				(fi->flags & ~CAMEL_FOLDER_TYPE_MASK) |
				CAMEL_FOLDER_TYPE_INBOX;
			fi->flags |= CAMEL_FOLDER_SYSTEM;
		}

		if (!(si->flags & CAMEL_FOLDER_NOSELECT))
			fill_fi ((CamelStore *) imapx_store, fi);

		if (!fi->child)
			fi->flags |= CAMEL_FOLDER_NOCHILDREN;

		g_ptr_array_add (folders, fi);
	}

	camel_store_summary_array_free (imapx_store->summary, array);

	fi = camel_folder_info_build (folders, top, '/', TRUE);

	g_ptr_array_free (folders, TRUE);

	return fi;
}

static void
collect_folder_info_for_list (CamelIMAPXStore *imapx_store,
                              CamelIMAPXMailbox *mailbox,
                              GHashTable *folder_info_results)
{
	CamelIMAPXStoreInfo *si;
	CamelFolderInfo *fi;
	const gchar *folder_path;
	const gchar *mailbox_name;

	mailbox_name = camel_imapx_mailbox_get_name (mailbox);

	si = camel_imapx_store_summary_mailbox (
		imapx_store->summary, mailbox_name);
	g_return_if_fail (si != NULL);

	folder_path = camel_store_info_path (
		imapx_store->summary, (CamelStoreInfo *) si);
	fi = imapx_store_build_folder_info (imapx_store, folder_path, 0);

	/* Takes ownership of the CamelFolderInfo. */
	g_hash_table_insert (folder_info_results, g_strdup (mailbox_name), fi);
}

static gboolean
fetch_folder_info_for_pattern (CamelIMAPXServer *server,
                               CamelIMAPXNamespace *namespace,
                               const gchar *pattern,
                               CamelStoreGetFolderInfoFlags flags,
                               GHashTable *folder_info_results,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelIMAPXStore *imapx_store;
	GList *list, *link;
	GError *local_error = NULL;
	gboolean success;

	g_object_ref (server);

	imapx_store = camel_imapx_server_ref_store (server);

	success = camel_imapx_server_list (server, pattern, flags, cancellable, &local_error);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&server);

		server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
		if (server)
			success = camel_imapx_server_list (server, pattern, flags, cancellable, &local_error);
	}

	g_clear_object (&server);

	if (!success) {
		g_clear_object (&imapx_store);

		if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
		    camel_imapx_namespace_get_category (namespace) != CAMEL_IMAPX_NAMESPACE_PERSONAL) {
			/* Ignore errors for non-personal namespaces; one such error can be:
			   "NO LIST failed: wildcards not permitted in username" */
			g_clear_error (&local_error);
			return TRUE;
		} else if (local_error) {
			g_propagate_error (error, local_error);
		}

		return FALSE;
	}

	list = camel_imapx_store_list_mailboxes (imapx_store, namespace, pattern);

	for (link = list; link != NULL; link = g_list_next (link)) {
		CamelIMAPXMailbox *mailbox;

		mailbox = CAMEL_IMAPX_MAILBOX (link->data);

		collect_folder_info_for_list (
			imapx_store, mailbox, folder_info_results);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	g_object_unref (imapx_store);

	return TRUE;
}

static gboolean
fetch_folder_info_for_inbox (CamelIMAPXServer *server,
                             CamelStoreGetFolderInfoFlags flags,
                             GHashTable *folder_info_results,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelIMAPXStore *imapx_store;
	GError *local_error = NULL;
	gboolean success;

	g_object_ref (server);
	imapx_store = camel_imapx_server_ref_store (server);

	success = camel_imapx_server_list (server, "INBOX", flags, cancellable, &local_error);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&server);

		server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
		if (server)
			success = camel_imapx_server_list (server, "INBOX", flags, cancellable, &local_error);
	}

	g_clear_object (&server);

	if (local_error)
		g_propagate_error (error, local_error);

	if (success) {
		CamelIMAPXMailbox *mailbox;

		mailbox = camel_imapx_store_ref_mailbox (imapx_store, "INBOX");
		g_return_val_if_fail (mailbox != NULL, FALSE);

		collect_folder_info_for_list (
			imapx_store, mailbox, folder_info_results);
	}

	g_object_unref (imapx_store);

	return success;
}

static gboolean
fetch_folder_info_for_namespace_category (CamelIMAPXStore *imapx_store,
					  CamelIMAPXServer *server,
                                          CamelIMAPXNamespaceCategory category,
                                          CamelStoreGetFolderInfoFlags flags,
                                          GHashTable *folder_info_results,
                                          GCancellable *cancellable,
                                          GError **error)
{
	CamelIMAPXNamespaceResponse *namespace_response;
	GList *list, *link;
	gboolean success = TRUE;

	namespace_response = camel_imapx_store_ref_namespaces (imapx_store);
	g_return_val_if_fail (namespace_response != NULL, FALSE);

	list = camel_imapx_namespace_response_list (namespace_response);

	for (link = list; link != NULL; link = g_list_next (link)) {
		CamelIMAPXNamespace *namespace;
		CamelIMAPXNamespaceCategory ns_category;
		const gchar *ns_prefix;
		gchar *pattern;

		namespace = CAMEL_IMAPX_NAMESPACE (link->data);
		ns_category = camel_imapx_namespace_get_category (namespace);
		ns_prefix = camel_imapx_namespace_get_prefix (namespace);

		if ((flags & (CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST | CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)) == 0 && ns_category != category)
			continue;

		pattern = g_strdup_printf ("%s*", ns_prefix);

		success = fetch_folder_info_for_pattern (
			server, namespace, pattern, flags,
			folder_info_results, cancellable, error);

		g_free (pattern);

		if (!success)
			break;
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	g_object_unref (namespace_response);

	return success;
}

static gboolean
fetch_folder_info_from_folder_path (CamelIMAPXStore *imapx_store,
				    CamelIMAPXServer *server,
                                    const gchar *folder_path,
                                    CamelStoreGetFolderInfoFlags flags,
                                    GHashTable *folder_info_results,
                                    GCancellable *cancellable,
                                    GError **error)
{
	CamelIMAPXNamespaceResponse *namespace_response;
	CamelIMAPXNamespace *namespace;
	gchar *mailbox_name;
	gchar *utf7_mailbox_name;
	gchar *pattern;
	gchar separator;
	gboolean success = FALSE;

	namespace_response = camel_imapx_store_ref_namespaces (imapx_store);
	g_return_val_if_fail (namespace_response != NULL, FALSE);

	/* Find a suitable IMAP namespace for the folder path. */
	namespace = camel_imapx_namespace_response_lookup_for_path (
		namespace_response, folder_path);
	if (namespace == NULL) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("No IMAP namespace for folder path '%s'"),
			folder_path);
		goto exit;
	}

	/* Convert the folder path to a mailbox name. */
	separator = camel_imapx_namespace_get_separator (namespace);
	mailbox_name = g_strdelimit (g_strdup (folder_path), "/", separator);

	utf7_mailbox_name = camel_utf8_utf7 (mailbox_name);
	pattern = g_strdup_printf ("%s*", utf7_mailbox_name);

	success = fetch_folder_info_for_pattern (
		server, namespace, pattern, flags,
		folder_info_results, cancellable, error);

	g_free (pattern);
	g_free (utf7_mailbox_name);
	g_free (mailbox_name);

exit:
	g_clear_object (&namespace);
	g_clear_object (&namespace_response);

	return success;
}

static void
imapx_store_mark_mailbox_unknown_cb (gpointer key,
				     gpointer value,
				     gpointer user_data)
{
	CamelIMAPXMailbox *mailbox = value;

	g_return_if_fail (mailbox != NULL);

	camel_imapx_mailbox_set_state (mailbox, CAMEL_IMAPX_MAILBOX_STATE_UNKNOWN);
}

static gboolean
imapx_store_remove_unknown_mailboxes_cb (gpointer key,
					 gpointer value,
					 gpointer user_data)
{
	CamelIMAPXMailbox *mailbox = value;
	CamelIMAPXStore *imapx_store = user_data;
	CamelStoreInfo *si;

	g_return_val_if_fail (mailbox != NULL, FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store), FALSE);

	if (camel_imapx_mailbox_get_state (mailbox) == CAMEL_IMAPX_MAILBOX_STATE_CREATED) {
		CamelFolderInfo *fi;
		gchar *folder_path;

		folder_path = camel_imapx_mailbox_dup_folder_path (mailbox);
		fi = imapx_store_build_folder_info (imapx_store, folder_path,
			imapx_store_mailbox_attributes_to_flags (mailbox));
		camel_store_folder_created (CAMEL_STORE (imapx_store), fi);
		camel_subscribable_folder_subscribed (CAMEL_SUBSCRIBABLE (imapx_store), fi);
		camel_folder_info_free (fi);
		g_free (folder_path);
	}

	if (camel_imapx_mailbox_get_state (mailbox) != CAMEL_IMAPX_MAILBOX_STATE_UNKNOWN) {
		return FALSE;
	}

	si = (CamelStoreInfo *) camel_imapx_store_summary_mailbox (imapx_store->summary, camel_imapx_mailbox_get_name (mailbox));
	if (si) {
		const gchar *si_path;
		gchar *dup_folder_path;

		si_path = camel_store_info_path (imapx_store->summary, si);
		dup_folder_path = g_strdup (si_path);

		if (dup_folder_path != NULL) {
			imapx_delete_folder_from_cache (imapx_store, dup_folder_path);
			g_free (dup_folder_path);
		} else {
			camel_store_summary_remove (imapx_store->summary, si);
		}

		camel_store_summary_info_unref (imapx_store->summary, si);
	}

	return TRUE;
}

static gboolean
sync_folders (CamelIMAPXStore *imapx_store,
              const gchar *root_folder_path,
              CamelStoreGetFolderInfoFlags flags,
	      gboolean initial_setup,
              GCancellable *cancellable,
              GError **error)
{
	CamelIMAPXServer *server;
	GHashTable *folder_info_results;
	GPtrArray *array;
	guint ii;
	gboolean success;

	server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, error);
	if (server == NULL)
		return FALSE;

	/* mailbox name -> CamelFolderInfo */
	folder_info_results = g_hash_table_new_full (
		(GHashFunc) imapx_name_hash,
		(GEqualFunc) imapx_name_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) camel_folder_info_free);

	/* This suppresses CamelStore signal emissions
	 * in imapx_store_process_mailbox_attributes(). */
	g_atomic_int_inc (&imapx_store->priv->syncing_folders);

	if (!initial_setup && (!root_folder_path || !*root_folder_path)) {
		g_mutex_lock (&imapx_store->priv->mailboxes_lock);
		g_hash_table_foreach (imapx_store->priv->mailboxes, imapx_store_mark_mailbox_unknown_cb, imapx_store);
		g_mutex_unlock (&imapx_store->priv->mailboxes_lock);
	}

	if (root_folder_path != NULL && *root_folder_path != '\0') {
		success = fetch_folder_info_from_folder_path (
			imapx_store, server, root_folder_path, flags,
			folder_info_results, cancellable, error);
	} else {
		gboolean have_folder_info_for_inbox;

		/* XXX We only fetch personal mailboxes at this time. */
		success = fetch_folder_info_for_namespace_category (
			imapx_store, server, CAMEL_IMAPX_NAMESPACE_PERSONAL, flags,
			folder_info_results, cancellable, error);

		have_folder_info_for_inbox =
			g_hash_table_contains (folder_info_results, "INBOX");

		/* XXX Slight hack, mainly for Courier servers.  If INBOX
		 *     is not included in any defined personal namespaces,
		 *     then LIST it explicitly. */
		if (success && !have_folder_info_for_inbox)
			success = fetch_folder_info_for_inbox (
				server, flags, folder_info_results,
				cancellable, error);
	}

	/* Don't need to test for zero, just decrement atomically. */
	g_atomic_int_dec_and_test (&imapx_store->priv->syncing_folders);

	if (!success)
		goto exit;

	if (!initial_setup && (!root_folder_path || !*root_folder_path)) {
		g_mutex_lock (&imapx_store->priv->mailboxes_lock);
		g_hash_table_foreach_remove (imapx_store->priv->mailboxes, imapx_store_remove_unknown_mailboxes_cb, imapx_store);
		g_mutex_unlock (&imapx_store->priv->mailboxes_lock);
	}

	array = camel_store_summary_array (imapx_store->summary);

	for (ii = 0; ii < array->len; ii++) {
		CamelStoreInfo *si;
		CamelFolderInfo *fi;
		const gchar *mailbox_name;
		const gchar *si_path;
		gboolean pattern_match;

		si = g_ptr_array_index (array, ii);
		si_path = camel_store_info_path (imapx_store->summary, si);

		mailbox_name = ((CamelIMAPXStoreInfo *) si)->mailbox_name;
		if (mailbox_name == NULL || *mailbox_name == '\0')
			continue;

		pattern_match =
			(root_folder_path == NULL) ||
			(*root_folder_path == '\0') ||
			(g_str_has_prefix (si_path, root_folder_path));
		if (!pattern_match)
			continue;

		fi = g_hash_table_lookup (folder_info_results, mailbox_name);

		if (fi == NULL) {
			gchar *dup_folder_path = g_strdup (si_path);

			if (dup_folder_path != NULL) {
				/* Do not unsubscribe from it, it influences UI for non-subscribable folders */
				imapx_delete_folder_from_cache (
					imapx_store, dup_folder_path);
				g_free (dup_folder_path);
			} else {
				camel_store_summary_remove (
					imapx_store->summary, si);
			}
		}
	}

	camel_store_summary_array_free (imapx_store->summary, array);

exit:
	g_hash_table_destroy (folder_info_results);

	g_object_unref (server);

	return success;
}

static void
imapx_refresh_finfo (CamelSession *session,
                     GCancellable *cancellable,
                     CamelIMAPXStore *store,
                     GError **error)
{
	CamelService *service;
	const gchar *display_name;

	service = CAMEL_SERVICE (store);
	display_name = camel_service_get_display_name (service);

	camel_operation_push_message (
		cancellable, _("Retrieving folder list for %s"),
		display_name);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)))
		goto exit;

	if (!camel_service_connect_sync (
		CAMEL_SERVICE (store), cancellable, error))
		goto exit;

	sync_folders (store, NULL, 0, FALSE, cancellable, error);

	camel_store_summary_save (store->summary);

exit:
	camel_operation_pop_message (cancellable);
}

static void
discover_inbox (CamelIMAPXStore *imapx_store,
                GCancellable *cancellable)
{
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	const gchar *attribute;

	imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, NULL);

	if (imapx_server == NULL)
		return;

	mailbox = camel_imapx_store_ref_mailbox (imapx_store, "INBOX");

	if (mailbox == NULL)
		goto exit;

	attribute = CAMEL_IMAPX_LIST_ATTR_SUBSCRIBED;
	if (!camel_imapx_mailbox_has_attribute (mailbox, attribute)) {
		GError *local_error = NULL;
		gboolean success;

		success = camel_imapx_server_subscribe_mailbox (imapx_server, mailbox, cancellable, &local_error);

		while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
			g_clear_error (&local_error);
			g_clear_object (&imapx_server);

			imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
			if (imapx_server)
				success = camel_imapx_server_subscribe_mailbox (imapx_server, mailbox, cancellable, &local_error);
		}

		g_clear_error (&local_error);
	}

exit:
	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);
}

static gboolean
imapx_can_refresh_folder (CamelStore *store,
                          CamelFolderInfo *info,
                          GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	CamelStoreClass *store_class;
	gboolean check_all;
	gboolean check_subscribed;
	gboolean subscribed;
	gboolean res;
	GError *local_error = NULL;

	store_class = CAMEL_STORE_CLASS (camel_imapx_store_parent_class);

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	check_all = camel_imapx_settings_get_check_all (
		CAMEL_IMAPX_SETTINGS (settings));

	check_subscribed = camel_imapx_settings_get_check_subscribed (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	subscribed = ((info->flags & CAMEL_FOLDER_SUBSCRIBED) != 0);

	res = store_class->can_refresh_folder (store, info, &local_error) ||
		check_all || (check_subscribed && subscribed);

	if (local_error != NULL)
		g_propagate_error (error, local_error);

	return res;
}

static CamelFolder *
imapx_store_get_folder_sync (CamelStore *store,
                             const gchar *folder_name,
                             CamelStoreGetFolderFlags flags,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelFolder *folder;
	CamelSettings *settings;
	gboolean use_real_junk_path = FALSE;
	gboolean use_real_trash_path = FALSE;

	/* XXX This should be taken care of before we get this far. */
	if (*folder_name == '/')
		folder_name++;

	folder = get_folder_offline (store, folder_name, flags, error);

	/* Configure the folder flags according to IMAPX settings.
	 *
	 * XXX Since this is only done when the folder is first created,
	 *     a restart is required to pick up changes to real Junk/Trash
	 *     folder settings.  Need to think of a better way.
	 *
	 *     Perhaps have CamelStoreSettings grow junk and trash path
	 *     string properties, and eliminate the CAMEL_FOLDER_IS_JUNK
	 *     and CAMEL_FOLDER_IS_TRASH flags.  Then add functions like
	 *     camel_folder_is_junk() and camel_folder_is_trash(), which
	 *     compare their own full name against CamelStoreSettings.
	 *
	 *     Something to think about...
	 */

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	if (folder != NULL) {
		use_real_junk_path =
			camel_imapx_settings_get_use_real_junk_path (
			CAMEL_IMAPX_SETTINGS (settings));
		use_real_trash_path =
			camel_imapx_settings_get_use_real_trash_path (
			CAMEL_IMAPX_SETTINGS (settings));
	}

	if (use_real_junk_path) {
		gchar *real_junk_path;

		real_junk_path =
			camel_imapx_settings_dup_real_junk_path (
			CAMEL_IMAPX_SETTINGS (settings));

		/* So we can safely compare strings. */
		if (real_junk_path == NULL)
			real_junk_path = g_strdup ("");

		if (g_ascii_strcasecmp (real_junk_path, folder_name) == 0)
			folder->folder_flags |= CAMEL_FOLDER_IS_JUNK;

		g_free (real_junk_path);
	}

	if (use_real_trash_path) {
		gchar *real_trash_path;

		real_trash_path =
			camel_imapx_settings_dup_real_trash_path (
			CAMEL_IMAPX_SETTINGS (settings));

		/* So we can safely compare strings. */
		if (real_trash_path == NULL)
			real_trash_path = g_strdup ("");

		if (g_ascii_strcasecmp (real_trash_path, folder_name) == 0)
			folder->folder_flags |= CAMEL_FOLDER_IS_TRASH;

		g_free (real_trash_path);
	}

	g_object_unref (settings);

	return folder;
}

static CamelFolderInfo *
imapx_store_get_folder_info_sync (CamelStore *store,
                                  const gchar *top,
                                  CamelStoreGetFolderInfoFlags flags,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelIMAPXStore *imapx_store;
	CamelFolderInfo *fi = NULL;
	CamelService *service;
	CamelSettings *settings;
	gboolean initial_setup = FALSE;
	gboolean use_subscriptions;

	service = CAMEL_SERVICE (store);
	imapx_store = CAMEL_IMAPX_STORE (store);

	settings = camel_service_ref_settings (service);

	use_subscriptions = camel_imapx_settings_get_use_subscriptions (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	if (top == NULL)
		top = "";

	g_mutex_lock (&imapx_store->priv->get_finfo_lock);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		fi = get_folder_info_offline (store, top, flags, error);
		goto exit;
	}

	if (imapx_store->priv->last_refresh_time == 0) {
		imapx_store->priv->last_refresh_time = time (NULL);
		initial_setup = TRUE;
	}

	/* XXX I don't know why the SUBSCRIBED flag matters here. */
	if (!initial_setup && flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED) {
		time_t time_since_last_refresh;

		time_since_last_refresh =
			time (NULL) - imapx_store->priv->last_refresh_time;

		if (time_since_last_refresh > FINFO_REFRESH_INTERVAL) {
			CamelSession *session;

			imapx_store->priv->last_refresh_time = time (NULL);

			session = camel_service_ref_session (service);

			camel_session_submit_job (
				session, (CamelSessionCallback)
				imapx_refresh_finfo,
				g_object_ref (store),
				(GDestroyNotify) g_object_unref);

			g_object_unref (session);
		}
	}

	/* Avoid server interaction if the FAST flag is set. */
	if (!initial_setup && flags & CAMEL_STORE_FOLDER_INFO_FAST) {
		fi = get_folder_info_offline (store, top, flags, error);
		goto exit;
	}

	if (!sync_folders (imapx_store, top, flags, initial_setup, cancellable, error))
		goto exit;

	camel_store_summary_save (imapx_store->summary);

	/* ensure the INBOX is subscribed if lsub was preferred*/
	if (initial_setup && use_subscriptions)
		discover_inbox (imapx_store, cancellable);

	fi = get_folder_info_offline (store, top, flags, error);

exit:
	g_mutex_unlock (&imapx_store->priv->get_finfo_lock);

	return fi;
}

static CamelFolder *
imapx_store_get_junk_folder_sync (CamelStore *store,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelFolder *folder = NULL;
	CamelStoreClass *store_class;
	CamelSettings *settings;

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));
	if (camel_imapx_settings_get_use_real_junk_path (CAMEL_IMAPX_SETTINGS (settings))) {
		gchar *real_junk_path;

		real_junk_path = camel_imapx_settings_dup_real_junk_path (CAMEL_IMAPX_SETTINGS (settings));
		if (real_junk_path) {
			folder = camel_store_get_folder_sync (store, real_junk_path, 0, cancellable, NULL);
			g_free (real_junk_path);
		}
	}
	g_object_unref (settings);

	if (folder)
		return folder;

	store_class = CAMEL_STORE_CLASS (camel_imapx_store_parent_class);
	folder = store_class->get_junk_folder_sync (store, cancellable, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		CamelService *service;
		const gchar *user_cache_dir;
		gchar *state;

		service = CAMEL_SERVICE (store);
		user_cache_dir = camel_service_get_user_cache_dir (service);

		state = g_build_filename (
			user_cache_dir, "system", "Junk.cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free (state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static CamelFolder *
imapx_store_get_trash_folder_sync (CamelStore *store,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelFolder *folder = NULL;
	CamelStoreClass *store_class;
	CamelSettings *settings;

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));
	if (camel_imapx_settings_get_use_real_trash_path (CAMEL_IMAPX_SETTINGS (settings))) {
		gchar *real_trash_path;

		real_trash_path = camel_imapx_settings_dup_real_trash_path (CAMEL_IMAPX_SETTINGS (settings));
		if (real_trash_path) {
			folder = camel_store_get_folder_sync (store, real_trash_path, 0, cancellable, NULL);
			g_free (real_trash_path);
		}
	}
	g_object_unref (settings);

	if (folder)
		return folder;

	store_class = CAMEL_STORE_CLASS (camel_imapx_store_parent_class);
	folder = store_class->get_trash_folder_sync (store, cancellable, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		CamelService *service;
		const gchar *user_cache_dir;
		gchar *state;

		service = CAMEL_SERVICE (store);
		user_cache_dir = camel_service_get_user_cache_dir (service);

		state = g_build_filename (
			user_cache_dir, "system", "Trash.cmeta", NULL);

		camel_object_set_state_filename (object, state);
		g_free (state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static CamelFolderInfo *
imapx_store_create_folder_sync (CamelStore *store,
                                const gchar *parent_name,
                                const gchar *folder_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelIMAPXNamespaceResponse *namespace_response;
	CamelIMAPXNamespace *namespace;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelFolder *folder;
	CamelIMAPXMailbox *parent_mailbox = NULL;
	CamelFolderInfo *fi = NULL;
	GList *list;
	const gchar *namespace_prefix;
	const gchar *parent_mailbox_name;
	gchar *mailbox_name = NULL;
	gchar separator;
	gboolean success;
	GError *local_error = NULL;

	imapx_store = CAMEL_IMAPX_STORE (store);
	imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, error);

	if (imapx_server == NULL)
		return NULL;

	if (parent_name == NULL || *parent_name == '\0')
		goto check_namespace;

	/* Obtain the separator from the parent CamelIMAPXMailbox. */

	folder = camel_store_get_folder_sync (
		store, parent_name, 0, cancellable, error);

	if (folder != NULL) {
		parent_mailbox = camel_imapx_folder_list_mailbox (
			CAMEL_IMAPX_FOLDER (folder), cancellable, error);
		g_object_unref (folder);
	}

	if (parent_mailbox == NULL)
		goto exit;

	separator = camel_imapx_mailbox_get_separator (parent_mailbox);
	parent_mailbox_name = camel_imapx_mailbox_get_name (parent_mailbox);

	mailbox_name = g_strdup_printf (
		"%s%c%s", parent_mailbox_name, separator, folder_name);

	g_object_unref (parent_mailbox);

	goto check_separator;

check_namespace:

	/* Obtain the separator from the first personal namespace.
	 *
	 * FIXME The CamelFolder API provides no way to specify a
	 *       namespace prefix when creating a top-level mailbox,
	 *       This needs fixed to properly support IMAP namespaces.
	 */

	namespace_response = camel_imapx_store_ref_namespaces (imapx_store);
	g_return_val_if_fail (namespace_response != NULL, NULL);

	list = camel_imapx_namespace_response_list (namespace_response);
	g_return_val_if_fail (list != NULL, NULL);

	/* The namespace list is in the order received in the NAMESPACE
	 * response so the first element should be a personal namespace. */
	namespace = CAMEL_IMAPX_NAMESPACE (list->data);

	separator = camel_imapx_namespace_get_separator (namespace);
	namespace_prefix = camel_imapx_namespace_get_prefix (namespace);

	mailbox_name = g_strconcat (namespace_prefix, folder_name, NULL);

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
	g_object_unref (namespace_response);

check_separator:

	if (strchr (folder_name, separator) != NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_PATH,
			_("The folder name \"%s\" is invalid "
			"because it contains the character \"%c\""),
			folder_name, separator);
		goto exit;
	}

	/* This also LISTs the mailbox after creating it, which
	 * triggers the CamelIMAPXStore::mailbox-created signal
	 * and all the local processing that goes along with it. */
	success = camel_imapx_server_create_mailbox (imapx_server, mailbox_name, cancellable, &local_error);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
		if (imapx_server)
			success = camel_imapx_server_create_mailbox (imapx_server, mailbox_name, cancellable, &local_error);
	}

	if (local_error)
		g_propagate_error (error, local_error);

	if (success) {
		fi = imapx_store_build_folder_info (
			imapx_store, folder_name,
			CAMEL_FOLDER_NOCHILDREN);
	}

exit:
	g_free (mailbox_name);

	g_clear_object (&imapx_server);

	return fi;
}

static gboolean
imapx_store_delete_folder_sync (CamelStore *store,
                                const gchar *folder_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelFolder *folder;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	gboolean success = FALSE;
	GError *local_error = NULL;

	folder = camel_store_get_folder_sync (
		store, folder_name, 0, cancellable, error);

	if (folder == NULL)
		return FALSE;

	imapx_store = CAMEL_IMAPX_STORE (store);
	imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	mailbox = camel_imapx_folder_list_mailbox (
		CAMEL_IMAPX_FOLDER (folder), cancellable, error);
	if (mailbox == NULL)
		goto exit;

	success = camel_imapx_server_delete_mailbox (imapx_server, mailbox, cancellable, &local_error);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
		if (imapx_server)
			success = camel_imapx_server_delete_mailbox (imapx_server, mailbox, cancellable, &local_error);
	}

	if (local_error)
		g_propagate_error (error, local_error);

	if (success)
		imapx_delete_folder_from_cache (imapx_store, folder_name);

exit:
	g_clear_object (&folder);
	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);

	return success;
}

static gboolean
imapx_store_rename_folder_sync (CamelStore *store,
                                const gchar *old,
                                const gchar *new,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelFolder *folder;
	CamelService *service;
	CamelSettings *settings;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	CamelIMAPXMailbox *cloned_mailbox;
	gchar *new_mailbox_name = NULL;
	gchar separator;
	gboolean use_subscriptions;
	gboolean success = FALSE;
	GError *local_error = NULL;

	service = CAMEL_SERVICE (store);
	imapx_store = CAMEL_IMAPX_STORE (store);

	settings = camel_service_ref_settings (service);

	use_subscriptions = camel_imapx_settings_get_use_subscriptions (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	/* This suppresses CamelStore signal emissions
	 * in imapx_store_process_mailbox_attributes(). */
	g_atomic_int_inc (&imapx_store->priv->syncing_folders);

	imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	folder = camel_store_get_folder_sync (
		store, old, 0, cancellable, error);

	if (folder != NULL) {
		mailbox = camel_imapx_folder_list_mailbox (
			CAMEL_IMAPX_FOLDER (folder), cancellable, error);
		g_object_unref (folder);
	}

	if (mailbox == NULL)
		goto exit;

	/* Assume the renamed mailbox will remain in the same namespace,
	 * and therefore use the same separator character.  XXX I'm not
	 * sure if IMAP even allows inter-namespace mailbox renames. */
	separator = camel_imapx_mailbox_get_separator (mailbox);
	new_mailbox_name = camel_imapx_folder_path_to_mailbox (new, separator);

	if (use_subscriptions) {
		success = camel_imapx_server_unsubscribe_mailbox (imapx_server, mailbox, cancellable, &local_error);

		while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
			g_clear_error (&local_error);
			g_clear_object (&imapx_server);

			imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
			if (imapx_server)
				success = camel_imapx_server_unsubscribe_mailbox (imapx_server, mailbox, cancellable, &local_error);
		}

		g_clear_error (&local_error);
	}

	success = camel_imapx_server_rename_mailbox (imapx_server, mailbox, new_mailbox_name, cancellable, &local_error);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
		if (imapx_server)
			success = camel_imapx_server_rename_mailbox (imapx_server, mailbox, new_mailbox_name, cancellable, &local_error);
	}

	if (!success) {
		if (local_error)
			g_propagate_error (error, local_error);
		local_error = NULL;

		if (use_subscriptions) {
			gboolean success_2;

			success_2 = camel_imapx_server_subscribe_mailbox (imapx_server, mailbox, cancellable, &local_error);

			while (!success_2 && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
				g_clear_error (&local_error);
				g_clear_object (&imapx_server);

				imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
				if (imapx_server)
					success_2 = camel_imapx_server_subscribe_mailbox (imapx_server, mailbox, cancellable, &local_error);
			}

			g_clear_error (&local_error);
		}
		goto exit;
	}

	/* Rename summary, and handle broken server. */
	imapx_store_rename_folder_info (imapx_store, old, new);
	imapx_store_rename_storage_path (imapx_store, old, new);

	/* Create a cloned CamelIMAPXMailbox with the new mailbox name. */
	cloned_mailbox = camel_imapx_mailbox_clone (mailbox, new_mailbox_name);

	camel_imapx_folder_set_mailbox (
		CAMEL_IMAPX_FOLDER (folder), cloned_mailbox);

	if (use_subscriptions) {
		success = camel_imapx_server_subscribe_mailbox (imapx_server, cloned_mailbox, cancellable, &local_error);

		while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
			g_clear_error (&local_error);
			g_clear_object (&imapx_server);

			imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
			if (imapx_server)
				success = camel_imapx_server_subscribe_mailbox (imapx_server, cloned_mailbox, cancellable, &local_error);
		}

		if (local_error)
			g_propagate_error (error, local_error);
	}

	g_clear_object (&cloned_mailbox);

exit:
	g_free (new_mailbox_name);

	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);

	/* This enabled CamelStore signal emissions
	 * in imapx_store_process_mailbox_attributes() again. */
	g_atomic_int_dec_and_test (&imapx_store->priv->syncing_folders);

	return success;
}

static void
imapx_migrate_to_user_cache_dir (CamelService *service)
{
	const gchar *user_data_dir, *user_cache_dir;

	g_return_if_fail (service != NULL);
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	user_data_dir = camel_service_get_user_data_dir (service);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	g_return_if_fail (user_data_dir != NULL);
	g_return_if_fail (user_cache_dir != NULL);

	/* migrate only if the source directory exists and the destination doesn't */
	if (g_file_test (user_data_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR) &&
	    !g_file_test (user_cache_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		gchar *parent_dir;

		parent_dir = g_path_get_dirname (user_cache_dir);
		g_mkdir_with_parents (parent_dir, S_IRWXU);
		g_free (parent_dir);

		if (g_rename (user_data_dir, user_cache_dir) == -1 && errno != ENOENT)
			g_debug ("%s: Failed to migrate '%s' to '%s': %s", G_STRFUNC, user_data_dir, user_cache_dir, g_strerror (errno));
	}
}

static gboolean
imapx_store_initable_init (GInitable *initable,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelIMAPXStore *imapx_store;
	CamelStore *store;
	CamelService *service;
	const gchar *user_cache_dir;
	gchar *summary;

	imapx_store = CAMEL_IMAPX_STORE (initable);
	store = CAMEL_STORE (initable);
	service = CAMEL_SERVICE (initable);

	store->flags |= CAMEL_STORE_USE_CACHE_DIR;
	imapx_migrate_to_user_cache_dir (service);

	/* Chain up to parent interface's init() method. */
	if (!parent_initable_interface->init (initable, cancellable, error))
		return FALSE;

	service = CAMEL_SERVICE (initable);
	user_cache_dir = camel_service_get_user_cache_dir (service);

	imapx_store->summary =
		g_object_new (CAMEL_TYPE_IMAPX_STORE_SUMMARY, NULL);

	summary = g_build_filename (user_cache_dir, ".ev-store-summary", NULL);
	camel_store_summary_set_filename (imapx_store->summary, summary);
	if (camel_store_summary_load (imapx_store->summary) == -1) {
		camel_store_summary_touch (imapx_store->summary);
		camel_store_summary_save (imapx_store->summary);
	}

	g_free (summary);

	return TRUE;
}

static const gchar *
imapx_store_get_service_name (CamelNetworkService *service,
                              CamelNetworkSecurityMethod method)
{
	const gchar *service_name;

	switch (method) {
		case CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT:
			service_name = "imaps";
			break;

		default:
			service_name = "imap";
			break;
	}

	return service_name;
}

static guint16
imapx_store_get_default_port (CamelNetworkService *service,
                              CamelNetworkSecurityMethod method)
{
	guint16 default_port;

	switch (method) {
		case CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT:
			default_port = IMAPS_PORT;
			break;

		default:
			default_port = IMAP_PORT;
			break;
	}

	return default_port;
}

static gboolean
imapx_store_folder_is_subscribed (CamelSubscribable *subscribable,
                                  const gchar *folder_name)
{
	CamelIMAPXStore *imapx_store;
	CamelStoreInfo *si;
	gint is_subscribed = FALSE;

	imapx_store = CAMEL_IMAPX_STORE (subscribable);

	if (folder_name && *folder_name == '/')
		folder_name++;

	si = camel_store_summary_path (imapx_store->summary, folder_name);
	if (si != NULL) {
		if (si->flags & CAMEL_STORE_INFO_FOLDER_SUBSCRIBED)
			is_subscribed = TRUE;
		camel_store_summary_info_unref (imapx_store->summary, si);
	}

	return is_subscribed;
}

static void
imapx_ensure_parents_subscribed (CamelIMAPXStore *imapx_store,
				 const gchar *folder_name)
{
	GSList *parents = NULL, *iter;
	CamelSubscribable *subscribable;
	CamelFolderInfo *fi;
	gchar *parent, *sep;

	g_return_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store));
	g_return_if_fail (folder_name != NULL);

	subscribable = CAMEL_SUBSCRIBABLE (imapx_store);

	if (folder_name && *folder_name == '/')
		folder_name++;

	parent = g_strdup (folder_name);
	while (sep = strrchr (parent, '/'), sep) {
		*sep = '\0';

		fi = camel_folder_info_new ();

		fi->display_name = strrchr (parent, '/');
		if (fi->display_name != NULL)
			fi->display_name = g_strdup (fi->display_name + 1);
		else
			fi->display_name = g_strdup (parent);

		fi->full_name = g_strdup (parent);

		/* Since this is a "fake" folder node, it is not selectable. */
		fi->flags |= CAMEL_FOLDER_NOSELECT;

		parents = g_slist_prepend (parents, fi);
	}

	for (iter = parents; iter; iter = g_slist_next (iter)) {
		fi = iter->data;

		camel_subscribable_folder_subscribed (subscribable, fi);
		camel_folder_info_free (fi);
	}
}

static gboolean
imapx_store_subscribe_folder_sync (CamelSubscribable *subscribable,
                                   const gchar *folder_name,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelFolder *folder;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	gboolean success = FALSE;
	GError *local_error = NULL;

	imapx_store = CAMEL_IMAPX_STORE (subscribable);
	imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	folder = camel_store_get_folder_sync (
		CAMEL_STORE (subscribable),
		folder_name, 0, cancellable, error);

	if (folder != NULL) {
		mailbox = camel_imapx_folder_list_mailbox (
			CAMEL_IMAPX_FOLDER (folder), cancellable, error);
		g_object_unref (folder);
	}

	if (mailbox == NULL)
		goto exit;

	success = camel_imapx_server_subscribe_mailbox (imapx_server, mailbox, cancellable, &local_error);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
		if (imapx_server)
			success = camel_imapx_server_subscribe_mailbox (imapx_server, mailbox, cancellable, &local_error);
	}

	if (local_error)
		g_propagate_error (error, local_error);

	if (success) {
		CamelFolderInfo *fi;

		/* without this the folder is not visible if parents are not subscribed */
		imapx_ensure_parents_subscribed (imapx_store, folder_name);

		fi = imapx_store_build_folder_info (
			CAMEL_IMAPX_STORE (subscribable), folder_name, 0);
		camel_subscribable_folder_subscribed (subscribable, fi);
		camel_folder_info_free (fi);
	}

exit:
	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);

	return success;
}

static gboolean
imapx_store_unsubscribe_folder_sync (CamelSubscribable *subscribable,
                                     const gchar *folder_name,
                                     GCancellable *cancellable,
                                     GError **error)
{
	CamelFolder *folder;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXServer *imapx_server;
	CamelIMAPXMailbox *mailbox = NULL;
	gboolean success = FALSE;
	GError *local_error = NULL;

	imapx_store = CAMEL_IMAPX_STORE (subscribable);
	imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, error);

	if (imapx_server == NULL)
		goto exit;

	folder = camel_store_get_folder_sync (
		CAMEL_STORE (subscribable),
		folder_name, 0, cancellable, error);

	if (folder != NULL) {
		mailbox = camel_imapx_folder_list_mailbox (
			CAMEL_IMAPX_FOLDER (folder), cancellable, error);
		g_object_unref (folder);
	}

	if (mailbox == NULL)
		goto exit;

	success = camel_imapx_server_unsubscribe_mailbox (imapx_server, mailbox, cancellable, &local_error);

	while (!success && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		g_clear_error (&local_error);
		g_clear_object (&imapx_server);

		imapx_server = camel_imapx_store_ref_server (imapx_store, NULL, FALSE, cancellable, &local_error);
		if (imapx_server)
			success = camel_imapx_server_unsubscribe_mailbox (imapx_server, mailbox, cancellable, &local_error);
	}

	if (local_error)
		g_propagate_error (error, local_error);

	if (success) {
		CamelFolderInfo *fi;

		fi = imapx_store_build_folder_info (
			CAMEL_IMAPX_STORE (subscribable), folder_name, 0);
		camel_subscribable_folder_unsubscribed (subscribable, fi);
		camel_folder_info_free (fi);
	}

exit:
	g_clear_object (&mailbox);
	g_clear_object (&imapx_server);

	return success;
}

static void
imapx_store_mailbox_created (CamelIMAPXStore *imapx_store,
			     CamelIMAPXMailbox *mailbox)
{
	e (
		'*',
		"%s::mailbox-created (\"%s\")\n",
		G_OBJECT_TYPE_NAME (imapx_store),
		camel_imapx_mailbox_get_name (mailbox));

	imapx_store_add_mailbox_to_folder (imapx_store, mailbox);
	imapx_store_process_mailbox_attributes (imapx_store, mailbox, NULL);
}

static void
imapx_store_mailbox_renamed (CamelIMAPXStore *imapx_store,
			     CamelIMAPXMailbox *mailbox,
			     const gchar *oldname)
{
	e (
		'*',
		"%s::mailbox-renamed (\"%s\" -> \"%s\")\n",
		G_OBJECT_TYPE_NAME (imapx_store), oldname,
		camel_imapx_mailbox_get_name (mailbox));

	imapx_store_process_mailbox_attributes (imapx_store, mailbox, oldname);
	imapx_store_process_mailbox_status (imapx_store, mailbox);
}

static void
imapx_store_mailbox_updated (CamelIMAPXStore *imapx_store,
			     CamelIMAPXMailbox *mailbox)
{
	e (
		'*',
		"%s::mailbox-updated (\"%s\")\n",
		G_OBJECT_TYPE_NAME (imapx_store),
		camel_imapx_mailbox_get_name (mailbox));

	imapx_store_process_mailbox_attributes (imapx_store, mailbox, NULL);
	imapx_store_process_mailbox_status (imapx_store, mailbox);
}

static void
camel_imapx_store_class_init (CamelIMAPXStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	g_type_class_add_private (class, sizeof (CamelIMAPXStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_store_set_property;
	object_class->get_property = imapx_store_get_property;
	object_class->dispose = imapx_store_dispose;
	object_class->finalize = imapx_store_finalize;
	object_class->notify = imapx_store_notify;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_IMAPX_SETTINGS;
	service_class->get_name = imapx_get_name;
	service_class->connect_sync = imapx_connect_sync;
	service_class->disconnect_sync = imapx_disconnect_sync;
	service_class->authenticate_sync = imapx_authenticate_sync;
	service_class->query_auth_types_sync = imapx_query_auth_types_sync;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->hash_folder_name = imapx_name_hash;
	store_class->equal_folder_name = imapx_name_equal;
	store_class->can_refresh_folder = imapx_can_refresh_folder;
	store_class->get_folder_sync = imapx_store_get_folder_sync;
	store_class->get_folder_info_sync = imapx_store_get_folder_info_sync;
	store_class->get_junk_folder_sync = imapx_store_get_junk_folder_sync;
	store_class->get_trash_folder_sync = imapx_store_get_trash_folder_sync;
	store_class->create_folder_sync = imapx_store_create_folder_sync;
	store_class->delete_folder_sync = imapx_store_delete_folder_sync;
	store_class->rename_folder_sync = imapx_store_rename_folder_sync;

	class->mailbox_created = imapx_store_mailbox_created;
	class->mailbox_renamed = imapx_store_mailbox_renamed;
	class->mailbox_updated = imapx_store_mailbox_updated;

	/* Inherited from CamelNetworkService. */
	g_object_class_override_property (
		object_class,
		PROP_CONNECTABLE,
		"connectable");

	/* Inherited from CamelNetworkService. */
	g_object_class_override_property (
		object_class,
		PROP_HOST_REACHABLE,
		"host-reachable");

	signals[MAILBOX_CREATED] = g_signal_new (
		"mailbox-created",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelIMAPXStoreClass, mailbox_created),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		CAMEL_TYPE_IMAPX_MAILBOX);

	signals[MAILBOX_RENAMED] = g_signal_new (
		"mailbox-renamed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelIMAPXStoreClass, mailbox_renamed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		CAMEL_TYPE_IMAPX_MAILBOX,
		G_TYPE_STRING);

	signals[MAILBOX_UPDATED] = g_signal_new (
		"mailbox-updated",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelIMAPXStoreClass, mailbox_updated),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		CAMEL_TYPE_IMAPX_MAILBOX);
}

static void
camel_imapx_store_initable_init (GInitableIface *iface)
{
	parent_initable_interface = g_type_interface_peek_parent (iface);

	iface->init = imapx_store_initable_init;
}

static void
camel_network_service_init (CamelNetworkServiceInterface *iface)
{
	iface->get_service_name = imapx_store_get_service_name;
	iface->get_default_port = imapx_store_get_default_port;
}

static void
camel_subscribable_init (CamelSubscribableInterface *iface)
{
	iface->folder_is_subscribed = imapx_store_folder_is_subscribed;
	iface->subscribe_folder_sync = imapx_store_subscribe_folder_sync;
	iface->unsubscribe_folder_sync = imapx_store_unsubscribe_folder_sync;
}

static void
camel_imapx_store_init (CamelIMAPXStore *store)
{
	store->priv = CAMEL_IMAPX_STORE_GET_PRIVATE (store);

	store->priv->con_man = camel_imapx_conn_manager_new (CAMEL_STORE (store));

	g_mutex_init (&store->priv->get_finfo_lock);

	g_mutex_init (&store->priv->namespaces_lock);
	g_mutex_init (&store->priv->mailboxes_lock);
	/* Hash table key is owned by the CamelIMAPXMailbox. */
	store->priv->mailboxes = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);

	/* Initialize to zero to ensure we always obtain fresh folder
	 * info on startup.  See imapx_store_get_folder_info_sync(). */
	store->priv->last_refresh_time = 0;

	g_mutex_init (&store->priv->server_lock);

	store->priv->quota_info = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) camel_folder_quota_info_free);
	g_mutex_init (&store->priv->quota_info_lock);

	g_mutex_init (&store->priv->settings_lock);

	imapx_utils_init ();

	g_signal_connect (
		store, "notify::settings",
		G_CALLBACK (imapx_store_update_store_flags), NULL);
}

/**
 * camel_imapx_store_ref_server:
 * @store: a #CamelIMAPXStore
 * @folder_name: name of a folder, for which it'll be used; can be %NULL
 * @cancellable: a #GCancellable to use ofr possible new connection creation, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Returns the #CamelIMAPXServer for @store, if available.
 *
 * As a convenience, if the @store is not currently connected to an IMAP
 * server, the function sets @error to %CAMEL_SERVER_ERROR_UNAVAILABLE and
 * returns %NULL.  If an operation can possibly be executed while offline,
 * pass %NULL for @error.
 *
 * The returned #CamelIMAPXServer is referenced for thread-safety and must
 * be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #CamelIMAPXServer, or %NULL
 *
 * Since: 3.10
 **/
CamelIMAPXServer *
camel_imapx_store_ref_server (CamelIMAPXStore *store,
			      const gchar *folder_name,
			      gboolean for_expensive_job,
			      GCancellable *cancellable,
                              GError **error)
{
	CamelIMAPXServer *server = NULL;
	CamelSession *session;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STORE (store), NULL);

	session = camel_service_ref_session (CAMEL_SERVICE (store));

	if (camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store)) &&
	    camel_session_get_online (session))
		server = camel_imapx_conn_manager_get_connection (
			store->priv->con_man, folder_name, for_expensive_job, cancellable, &local_error);

	g_clear_object (&session);

	if (!server && (!local_error || local_error->domain == G_RESOLVER_ERROR)) {
		if (!local_error) {
			g_set_error (
				&local_error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("You must be working online to complete this operation"));
		} else {
			local_error->domain = CAMEL_SERVICE_ERROR;
			local_error->code = CAMEL_SERVICE_ERROR_UNAVAILABLE;
		}
	}

	if (local_error)
		g_propagate_error (error, local_error);

	return server;
}

/* The caller should hold the store->priv->server_lock already, when calling this */
void
camel_imapx_store_set_connecting_server (CamelIMAPXStore *store,
					 CamelIMAPXServer *server,
					 gboolean is_concurrent_connection)
{
	g_return_if_fail (CAMEL_IS_IMAPX_STORE (store));

	if (server)
		g_return_if_fail (CAMEL_IS_IMAPX_SERVER (server));

	g_mutex_lock (&store->priv->server_lock);

	if (store->priv->connecting_server != server) {
		g_clear_object (&store->priv->connecting_server);
		if (server)
			store->priv->connecting_server = g_object_ref (server);
	}

	store->priv->is_concurrent_connection = is_concurrent_connection;

	g_mutex_unlock (&store->priv->server_lock);
}

gboolean
camel_imapx_store_is_connecting_concurrent_connection (CamelIMAPXStore *imapx_store)
{
	gboolean res;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store), FALSE);

	g_mutex_lock (&imapx_store->priv->server_lock);
	res = imapx_store->priv->is_concurrent_connection;
	g_mutex_unlock (&imapx_store->priv->server_lock);

	return res;
}

void
camel_imapx_store_folder_op_done (CamelIMAPXStore *store,
				  CamelIMAPXServer *server,
				  const gchar *folder_name)
{
	g_return_if_fail (CAMEL_IS_IMAPX_STORE (store));
	g_return_if_fail (CAMEL_IS_IMAPX_SERVER (server));
	g_return_if_fail (folder_name != NULL);

	camel_imapx_conn_manager_update_con_info (
		store->priv->con_man, server, folder_name);
}

/**
 * camel_imapx_store_ref_namespaces:
 * @imapx_store: a #CamelIMAPXStore
 *
 * Returns the #CamelIMAPXNamespaceResponse for @is. This is obtained
 * during the connection phase if the IMAP server lists the "NAMESPACE"
 * keyword in its CAPABILITY response, or else is fabricated from the
 * first LIST response.
 *
 * The returned #CamelIMAPXNamespaceResponse is reference for thread-safety
 * and must be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #CamelIMAPXNamespaceResponse
 *
 * Since: 3.12.2
 **/
CamelIMAPXNamespaceResponse *
camel_imapx_store_ref_namespaces (CamelIMAPXStore *imapx_store)
{
	CamelIMAPXNamespaceResponse *namespaces = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store), NULL);

	g_mutex_lock (&imapx_store->priv->namespaces_lock);

	if (imapx_store->priv->namespaces != NULL)
		namespaces = g_object_ref (imapx_store->priv->namespaces);

	g_mutex_unlock (&imapx_store->priv->namespaces_lock);

	return namespaces;
}

void
camel_imapx_store_set_namespaces (CamelIMAPXStore *imapx_store,
				  CamelIMAPXNamespaceResponse *namespaces)
{
	g_return_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store));
	if (namespaces)
		g_return_if_fail (CAMEL_IS_IMAPX_NAMESPACE_RESPONSE (namespaces));

	if (namespaces)
		g_object_ref (namespaces);

	g_mutex_lock (&imapx_store->priv->namespaces_lock);

	g_clear_object (&imapx_store->priv->namespaces);
	imapx_store->priv->namespaces = namespaces;

	g_mutex_unlock (&imapx_store->priv->namespaces_lock);
}

static void
imapx_store_add_mailbox_unlocked (CamelIMAPXStore *imapx_store,
				  CamelIMAPXMailbox *mailbox)
{
	const gchar *mailbox_name;

	/* Acquire "mailboxes_lock" before calling. */

	mailbox_name = camel_imapx_mailbox_get_name (mailbox);
	g_return_if_fail (mailbox_name != NULL);

	/* Use g_hash_table_replace() here instead of g_hash_table_insert().
	 * The hash table key is owned by the hash table value, so if we're
	 * replacing an existing table item we want to replace both the key
	 * and value to avoid data corruption. */
	g_hash_table_replace (
		imapx_store->priv->mailboxes,
		(gpointer) mailbox_name,
		g_object_ref (mailbox));
}

static gboolean
imapx_store_remove_mailbox_unlocked (CamelIMAPXStore *imapx_store,
				     CamelIMAPXMailbox *mailbox)
{
	const gchar *mailbox_name;

	/* Acquire "mailboxes_lock" before calling. */

	mailbox_name = camel_imapx_mailbox_get_name (mailbox);
	g_return_val_if_fail (mailbox_name != NULL, FALSE);

	return g_hash_table_remove (imapx_store->priv->mailboxes, mailbox_name);
}

static CamelIMAPXMailbox *
imapx_store_ref_mailbox_unlocked (CamelIMAPXStore *imapx_store,
				  const gchar *mailbox_name)
{
	CamelIMAPXMailbox *mailbox;

	/* Acquire "mailboxes_lock" before calling. */

	g_return_val_if_fail (mailbox_name != NULL, NULL);

	/* The INBOX mailbox is case-insensitive. */
	if (g_ascii_strcasecmp (mailbox_name, "INBOX") == 0)
		mailbox_name = "INBOX";

	mailbox = g_hash_table_lookup (imapx_store->priv->mailboxes, mailbox_name);

	/* Remove non-existent mailboxes as we find them. */
	if (mailbox != NULL && !camel_imapx_mailbox_exists (mailbox)) {
		imapx_store_remove_mailbox_unlocked (imapx_store, mailbox);
		mailbox = NULL;
	}

	if (mailbox != NULL)
		g_object_ref (mailbox);

	return mailbox;
}

static GList *
imapx_store_list_mailboxes_unlocked (CamelIMAPXStore *imapx_store,
				     CamelIMAPXNamespace *namespace,
				     const gchar *pattern)
{
	GHashTableIter iter;
	GList *list = NULL;
	gpointer value;

	/* Acquire "mailboxes_lock" before calling. */

	if (pattern == NULL)
		pattern = "*";

	g_hash_table_iter_init (&iter, imapx_store->priv->mailboxes);

	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		CamelIMAPXMailbox *mailbox;
		CamelIMAPXNamespace *mailbox_ns;

		mailbox = CAMEL_IMAPX_MAILBOX (value);
		mailbox_ns = camel_imapx_mailbox_get_namespace (mailbox);

		if (!camel_imapx_mailbox_exists (mailbox))
			continue;

		if (!camel_imapx_namespace_equal (namespace, mailbox_ns))
			continue;

		if (!camel_imapx_mailbox_matches (mailbox, pattern))
			continue;

		list = g_list_prepend (list, g_object_ref (mailbox));
	}

	/* Sort the list by mailbox name. */
	return g_list_sort (list, (GCompareFunc) camel_imapx_mailbox_compare);
}

static CamelIMAPXMailbox *
imapx_store_create_mailbox_unlocked (CamelIMAPXStore *imapx_store,
				     CamelIMAPXListResponse *response)
{
	CamelIMAPXNamespaceResponse *namespace_response;
	CamelIMAPXNamespace *namespace;
	CamelIMAPXMailbox *mailbox = NULL;
	const gchar *mailbox_name;
	gchar separator;

	/* Acquire "mailboxes_lock" before calling. */

	namespace_response = camel_imapx_store_ref_namespaces (imapx_store);
	g_return_val_if_fail (namespace_response != NULL, FALSE);

	mailbox_name = camel_imapx_list_response_get_mailbox_name (response);
	separator = camel_imapx_list_response_get_separator (response);

	namespace = camel_imapx_namespace_response_lookup (
		namespace_response, mailbox_name, separator);

	if (namespace != NULL) {
		mailbox = camel_imapx_mailbox_new (response, namespace);
		imapx_store_add_mailbox_unlocked (imapx_store, mailbox);
		g_object_unref (namespace);

	/* XXX Slight hack, mainly for Courier servers.  If INBOX does
	 *     not match any defined namespace, just create one for it
	 *     on the fly.  The namespace response won't know about it. */
	} else if (camel_imapx_mailbox_is_inbox (mailbox_name)) {
		namespace = camel_imapx_namespace_new (
			CAMEL_IMAPX_NAMESPACE_PERSONAL, "", separator);
		mailbox = camel_imapx_mailbox_new (response, namespace);
		imapx_store_add_mailbox_unlocked (imapx_store, mailbox);
		g_object_unref (namespace);

	} else {
		g_warning (
			"%s: No matching namespace for \"%c\" %s",
			G_STRFUNC, separator, mailbox_name);
	}

	g_object_unref (namespace_response);

	return mailbox;
}

static CamelIMAPXMailbox *
imapx_store_rename_mailbox_unlocked (CamelIMAPXStore *imapx_store,
				     const gchar *old_mailbox_name,
				     const gchar *new_mailbox_name)
{
	CamelIMAPXMailbox *old_mailbox;
	CamelIMAPXMailbox *new_mailbox;
	CamelIMAPXNamespace *namespace;
	gsize old_mailbox_name_length;
	GList *list, *link;
	gchar separator;
	gchar *pattern;

	/* Acquire "mailboxes_lock" before calling. */

	g_return_val_if_fail (old_mailbox_name != NULL, NULL);
	g_return_val_if_fail (new_mailbox_name != NULL, NULL);

	old_mailbox = imapx_store_ref_mailbox_unlocked (imapx_store, old_mailbox_name);
	if (old_mailbox == NULL)
		return NULL;

	old_mailbox_name_length = strlen (old_mailbox_name);
	namespace = camel_imapx_mailbox_get_namespace (old_mailbox);
	separator = camel_imapx_mailbox_get_separator (old_mailbox);

	new_mailbox = camel_imapx_mailbox_clone (old_mailbox, new_mailbox_name);

	/* Add the new mailbox, remove the old mailbox.
	 * Note we still have a reference on the old mailbox. */
	imapx_store_add_mailbox_unlocked (imapx_store, new_mailbox);
	imapx_store_remove_mailbox_unlocked (imapx_store, old_mailbox);

	/* Rename any child mailboxes. */

	pattern = g_strdup_printf ("%s%c*", old_mailbox_name, separator);
	list = imapx_store_list_mailboxes_unlocked (imapx_store, namespace, pattern);

	for (link = list; link != NULL; link = g_list_next (link)) {
		CamelIMAPXMailbox *old_child;
		CamelIMAPXMailbox *new_child;
		const gchar *old_child_name;
		gchar *new_child_name;

		old_child = CAMEL_IMAPX_MAILBOX (link->data);
		old_child_name = camel_imapx_mailbox_get_name (old_child);

		/* Sanity checks. */
		g_warn_if_fail (
			old_child_name != NULL &&
			strlen (old_child_name) > old_mailbox_name_length &&
			old_child_name[old_mailbox_name_length] == separator);

		new_child_name = g_strconcat (
			new_mailbox_name,
			old_child_name + old_mailbox_name_length, NULL);
		new_child = camel_imapx_mailbox_clone (
			old_child, new_child_name);

		/* Add the new mailbox, remove the old mailbox.
		 * Note we still have a reference on the old mailbox. */
		imapx_store_add_mailbox_unlocked (imapx_store, new_child);
		imapx_store_remove_mailbox_unlocked (imapx_store, old_child);

		g_object_unref (new_child);
		g_free (new_child_name);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
	g_free (pattern);

	g_object_unref (old_mailbox);

	return new_mailbox;
}

/**
 * camel_imapx_store_ref_mailbox:
 * @imapx_store: a #CamelIMAPXStore
 * @mailbox_name: a mailbox name
 *
 * Looks up a #CamelMailbox by its name. If no match is found, the function
 * returns %NULL.
 *
 * The returned #CamelIMAPXMailbox is referenced for thread-safety and
 * should be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #CamelIMAPXMailbox, or %NULL
 *
 * Since: 3.12.2
 **/
CamelIMAPXMailbox *
camel_imapx_store_ref_mailbox (CamelIMAPXStore *imapx_store,
			       const gchar *mailbox_name)
{
	CamelIMAPXMailbox *mailbox;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store), NULL);
	g_return_val_if_fail (mailbox_name != NULL, NULL);

	g_mutex_lock (&imapx_store->priv->mailboxes_lock);

	mailbox = imapx_store_ref_mailbox_unlocked (imapx_store, mailbox_name);

	g_mutex_unlock (&imapx_store->priv->mailboxes_lock);

	return mailbox;
}

/**
 * camel_imapx_store_list_mailboxes:
 * @imapx_store: a #CamelIMAPXStore
 * @namespace_: a #CamelIMAPXNamespace
 * @pattern: mailbox name with possible wildcards, or %NULL
 *
 * Returns a list of #CamelIMAPXMailbox instances which match @namespace and
 * @pattern. The @pattern may contain wildcard characters '*' and '%', which
 * are interpreted similar to the IMAP LIST command. A %NULL @pattern lists
 * all mailboxes in @namespace; equivalent to passing "*".
 *
 * The mailboxes returned in the list are referenced for thread-safety.
 * They must each be unreferenced with g_object_unref() when finished with
 * them. Free the returned list itself with g_list_free().
 *
 * An easy way to free the list properly in one step is as follows:
 *
 * |[
 *   g_list_free_full (list, g_object_unref);
 * ]|
 *
 * Returns: a list of #CamelIMAPXMailbox instances
 *
 * Since: 3.12.2
 **/
GList *
camel_imapx_store_list_mailboxes (CamelIMAPXStore *imapx_store,
				  CamelIMAPXNamespace *namespace,
				  const gchar *pattern)
{
	GList *list;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store), NULL);
	g_return_val_if_fail (CAMEL_IS_IMAPX_NAMESPACE (namespace), NULL);

	g_mutex_lock (&imapx_store->priv->mailboxes_lock);

	list = imapx_store_list_mailboxes_unlocked (imapx_store, namespace, pattern);

	g_mutex_unlock (&imapx_store->priv->mailboxes_lock);

	return list;
}

void
camel_imapx_store_emit_mailbox_updated (CamelIMAPXStore *imapx_store,
					CamelIMAPXMailbox *mailbox)
{
	g_return_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store));
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (mailbox));

	g_signal_emit (imapx_store, signals[MAILBOX_UPDATED], 0, mailbox);
}

void
camel_imapx_store_handle_mailbox_rename (CamelIMAPXStore *imapx_store,
					 CamelIMAPXMailbox *old_mailbox,
					 const gchar *new_mailbox_name)
{
	CamelIMAPXMailbox *new_mailbox;
	const gchar *old_mailbox_name;

	g_return_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store));
	g_return_if_fail (CAMEL_IS_IMAPX_MAILBOX (old_mailbox));
	g_return_if_fail (new_mailbox_name != NULL);

	old_mailbox_name = camel_imapx_mailbox_get_name (old_mailbox);

	g_mutex_lock (&imapx_store->priv->mailboxes_lock);
	new_mailbox = imapx_store_rename_mailbox_unlocked (
		imapx_store, old_mailbox_name, new_mailbox_name);
	g_mutex_unlock (&imapx_store->priv->mailboxes_lock);

	g_warn_if_fail (new_mailbox != NULL);

	g_signal_emit (
		imapx_store, signals[MAILBOX_RENAMED], 0,
		new_mailbox, old_mailbox_name);

	g_clear_object (&new_mailbox);
}

void
camel_imapx_store_handle_list_response (CamelIMAPXStore *imapx_store,
					CamelIMAPXServer *imapx_server,
					CamelIMAPXListResponse *response)
{
	CamelIMAPXMailbox *mailbox = NULL;
	const gchar *mailbox_name;
	const gchar *old_mailbox_name;
	gboolean emit_mailbox_created = FALSE;
	gboolean emit_mailbox_renamed = FALSE;
	gboolean emit_mailbox_updated = FALSE;

	g_return_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store));
	g_return_if_fail (CAMEL_IS_IMAPX_SERVER (imapx_server));
	g_return_if_fail (CAMEL_IS_IMAPX_LIST_RESPONSE (response));

	mailbox_name = camel_imapx_list_response_get_mailbox_name (response);
	old_mailbox_name = camel_imapx_list_response_get_oldname (response);

	/* Fabricate a CamelIMAPXNamespaceResponse if the server lacks the
	 * NAMESPACE capability and this is the first LIST / LSUB response. */
	if (CAMEL_IMAPX_LACK_CAPABILITY (imapx_server->cinfo, NAMESPACE)) {
		g_mutex_lock (&imapx_store->priv->namespaces_lock);
		if (imapx_store->priv->namespaces == NULL) {
			imapx_store->priv->namespaces = camel_imapx_namespace_response_faux_new (response);
		}
		g_mutex_unlock (&imapx_store->priv->namespaces_lock);
	}

	/* Create, rename, or update a corresponding CamelIMAPXMailbox. */
	g_mutex_lock (&imapx_store->priv->mailboxes_lock);
	if (old_mailbox_name != NULL) {
		mailbox = imapx_store_rename_mailbox_unlocked (
			imapx_store, old_mailbox_name, mailbox_name);
		emit_mailbox_renamed = (mailbox != NULL);
		if (mailbox && camel_imapx_mailbox_get_state (mailbox) == CAMEL_IMAPX_MAILBOX_STATE_UNKNOWN)
			camel_imapx_mailbox_set_state (mailbox, CAMEL_IMAPX_MAILBOX_STATE_RENAMED);
	}
	if (mailbox == NULL) {
		mailbox = imapx_store_ref_mailbox_unlocked (imapx_store, mailbox_name);
		emit_mailbox_updated = (mailbox != NULL);
		if (mailbox && camel_imapx_mailbox_get_state (mailbox) == CAMEL_IMAPX_MAILBOX_STATE_UNKNOWN)
			camel_imapx_mailbox_set_state (mailbox, CAMEL_IMAPX_MAILBOX_STATE_UPDATED);
	}
	if (mailbox == NULL) {
		mailbox = imapx_store_create_mailbox_unlocked (imapx_store, response);
		emit_mailbox_created = (mailbox != NULL);
		if (mailbox)
			camel_imapx_mailbox_set_state (mailbox, CAMEL_IMAPX_MAILBOX_STATE_CREATED);
	} else {
		camel_imapx_mailbox_handle_list_response (mailbox, response);
	}
	g_mutex_unlock (&imapx_store->priv->mailboxes_lock);

	if (emit_mailbox_created)
		g_signal_emit (imapx_store, signals[MAILBOX_CREATED], 0, mailbox);

	if (emit_mailbox_renamed)
		g_signal_emit (
			imapx_store, signals[MAILBOX_RENAMED], 0,
			mailbox, old_mailbox_name);

	if (emit_mailbox_updated)
		g_signal_emit (imapx_store, signals[MAILBOX_UPDATED], 0, mailbox);

	g_clear_object (&mailbox);
}

void
camel_imapx_store_handle_lsub_response (CamelIMAPXStore *imapx_store,
					CamelIMAPXServer *imapx_server,
					CamelIMAPXListResponse *response)
{
	CamelIMAPXMailbox *mailbox;
	const gchar *mailbox_name;
	gboolean emit_mailbox_updated = FALSE;

	g_return_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store));
	g_return_if_fail (CAMEL_IS_IMAPX_SERVER (imapx_server));
	g_return_if_fail (CAMEL_IS_IMAPX_LIST_RESPONSE (response));

	mailbox_name = camel_imapx_list_response_get_mailbox_name (response);

	/* Fabricate a CamelIMAPXNamespaceResponse if the server lacks the
	 * NAMESPACE capability and this is the first LIST / LSUB response. */
	if (CAMEL_IMAPX_LACK_CAPABILITY (imapx_server->cinfo, NAMESPACE)) {
		g_mutex_lock (&imapx_store->priv->namespaces_lock);
		if (imapx_store->priv->namespaces == NULL) {
			imapx_store->priv->namespaces = camel_imapx_namespace_response_faux_new (response);
		}
		g_mutex_unlock (&imapx_store->priv->namespaces_lock);
	}

	/* Update a corresponding CamelIMAPXMailbox.
	 *
	 * Note, don't create the CamelIMAPXMailbox like we do for a LIST
	 * response.  We always issue LIST before LSUB on a mailbox name,
	 * so if we don't already have a CamelIMAPXMailbox instance then
	 * this is a subscription on a non-existent mailbox.  Skip it. */
	g_mutex_lock (&imapx_store->priv->mailboxes_lock);
	mailbox = imapx_store_ref_mailbox_unlocked (imapx_store, mailbox_name);
	if (mailbox != NULL) {
		camel_imapx_mailbox_handle_lsub_response (mailbox, response);
		if (camel_imapx_mailbox_get_state (mailbox) == CAMEL_IMAPX_MAILBOX_STATE_UNKNOWN)
			camel_imapx_mailbox_set_state (mailbox, CAMEL_IMAPX_MAILBOX_STATE_UPDATED);
		emit_mailbox_updated = TRUE;
	}
	g_mutex_unlock (&imapx_store->priv->mailboxes_lock);

	if (emit_mailbox_updated)
		g_signal_emit (imapx_store, signals[MAILBOX_UPDATED], 0, mailbox);

	g_clear_object (&mailbox);
}

CamelFolderQuotaInfo *
camel_imapx_store_dup_quota_info (CamelIMAPXStore *store,
                                  const gchar *quota_root_name)
{
	CamelFolderQuotaInfo *info;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STORE (store), NULL);
	g_return_val_if_fail (quota_root_name != NULL, NULL);

	g_mutex_lock (&store->priv->quota_info_lock);

	info = g_hash_table_lookup (
		store->priv->quota_info, quota_root_name);

	/* camel_folder_quota_info_clone() handles NULL gracefully. */
	info = camel_folder_quota_info_clone (info);

	g_mutex_unlock (&store->priv->quota_info_lock);

	return info;
}

void
camel_imapx_store_set_quota_info (CamelIMAPXStore *store,
                                  const gchar *quota_root_name,
                                  const CamelFolderQuotaInfo *info)
{
	g_return_if_fail (CAMEL_IS_IMAPX_STORE (store));
	g_return_if_fail (quota_root_name != NULL);

	g_mutex_lock (&store->priv->quota_info_lock);

	/* camel_folder_quota_info_clone() handles NULL gracefully. */
	g_hash_table_insert (
		store->priv->quota_info,
		g_strdup (quota_root_name),
		camel_folder_quota_info_clone (info));

	g_mutex_unlock (&store->priv->quota_info_lock);
}

/* Tries to find matching job among all active connections.
   See camel_imapx_server_ref_job() for more information on parameters
   and return values.
*/
CamelIMAPXJob *
camel_imapx_store_ref_job (CamelIMAPXStore *imapx_store,
			   CamelIMAPXMailbox *mailbox,
			   guint32 job_type,
			   const gchar *uid)
{
	GList *servers, *siter;
	CamelIMAPXJob *job = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store), NULL);

	servers = camel_imapx_conn_manager_get_connections (imapx_store->priv->con_man);

	for (siter = servers; siter; siter = g_list_next (siter)) {
		CamelIMAPXServer *imapx_server = siter->data;

		job = camel_imapx_server_ref_job (imapx_server, mailbox, job_type, uid);
		if (job)
			break;
	}

	g_list_free_full (servers, g_object_unref);

	return job;
}

/* for debugging purposes only */
void
camel_imapx_store_dump_queue_status (CamelIMAPXStore *imapx_store)
{
	g_return_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store));

	camel_imapx_conn_manager_dump_queue_status (imapx_store->priv->con_man);
}

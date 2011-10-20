/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include <libedataserver/e-data-server-util.h>

#include "camel-local-folder.h"
#include "camel-local-store.h"

#define d(x)

#define CAMEL_LOCAL_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_LOCAL_STORE, CamelLocalStorePrivate))

struct _CamelLocalStorePrivate {
	gboolean need_summary_check;
};

enum {
	PROP_0,
	PROP_NEED_SUMMARY_CHECK
};

G_DEFINE_TYPE (CamelLocalStore, camel_local_store, CAMEL_TYPE_STORE)

static gint
xrename (const gchar *oldp,
         const gchar *newp,
         const gchar *prefix,
         const gchar *suffix,
         gint missingok,
         GError **error)
{
	struct stat st;
	gchar *old, *new;
	gchar *basename;
	gint ret = -1;
	gint err = 0;

	basename = g_strconcat (oldp, suffix, NULL);
	old = g_build_filename (prefix, basename, NULL);
	g_free (basename);

	basename = g_strconcat (newp, suffix, NULL);
	new = g_build_filename (prefix, basename, NULL);
	g_free (basename);

	d(printf("renaming %s%s to %s%s\n", oldp, suffix, newp, suffix));

	if (g_stat (old, &st) == -1) {
		if (missingok && errno == ENOENT) {
			ret = 0;
		} else {
			err = errno;
			ret = -1;
		}
	} else if ((!g_file_test (new, G_FILE_TEST_EXISTS) || g_remove (new) == 0) &&
		   g_rename (old, new) == 0) {
		ret = 0;
	} else {
		err = errno;
		ret = -1;
	}

	if (ret == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (err),
			_("Could not rename folder %s to %s: %s"),
			old, new, g_strerror (err));
	}

	g_free (old);
	g_free (new);
	return ret;
}

static void
local_store_set_property (GObject *object,
                          guint property_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_NEED_SUMMARY_CHECK:
			camel_local_store_set_need_summary_check (
				CAMEL_LOCAL_STORE (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
local_store_get_property (GObject *object,
                          guint property_id,
                          GValue *value,
                          GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_NEED_SUMMARY_CHECK:
			g_value_set_boolean (
				value,
				camel_local_store_get_need_summary_check (
				CAMEL_LOCAL_STORE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
local_store_constructed (GObject *object)
{
	CamelLocalStore *local_store;
	CamelService *service;
	const gchar *uid;

	local_store = CAMEL_LOCAL_STORE (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (camel_local_store_parent_class)->constructed (object);

	service = CAMEL_SERVICE (object);
	uid = camel_service_get_uid (service);

	/* XXX This is Evolution-specific policy. */
	local_store->is_main_store = (g_strcmp0 (uid, "local") == 0);
}

static gchar *
local_store_get_name (CamelService *service,
                      gboolean brief)
{
	CamelLocalSettings *local_settings;
	CamelSettings *settings;
	const gchar *path;

	settings = camel_service_get_settings (service);

	local_settings = CAMEL_LOCAL_SETTINGS (settings);
	path = camel_local_settings_get_path (local_settings);

	if (brief)
		return g_strdup (path);
	else
		return g_strdup_printf (_("Local mail file %s"), path);
}

static gboolean
local_store_can_refresh_folder (CamelStore *store,
                                CamelFolderInfo *info,
                                GError **error)
{
	/* any local folder can be refreshed */
	return TRUE;
}

static CamelFolder *
local_store_get_folder_sync (CamelStore *store,
                             const gchar *folder_name,
                             CamelStoreGetFolderFlags flags,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelLocalSettings *local_settings;
	CamelSettings *settings;
	CamelService *service;
	const gchar *path;
	struct stat st;

	service = CAMEL_SERVICE (store);
	settings= camel_service_get_settings (service);

	local_settings = CAMEL_LOCAL_SETTINGS (settings);
	path = camel_local_settings_get_path (local_settings);

	if (!g_path_is_absolute (path)) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Store root %s is not an absolute path"), path);
		return NULL;
	}

	if (g_stat (path, &st) == 0) {
		if (!S_ISDIR (st.st_mode)) {
			g_set_error (
				error, CAMEL_STORE_ERROR,
				CAMEL_STORE_ERROR_NO_FOLDER,
				_("Store root %s is not a regular directory"), path);
			return NULL;
		}
		return (CamelFolder *) 0xdeadbeef;
	}

	if (errno != ENOENT
	    || (flags & CAMEL_STORE_FOLDER_CREATE) == 0) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Cannot get folder: %s: %s"),
			path, g_strerror (errno));
		return NULL;
	}

	/* need to create the dir heirarchy */
	if (g_mkdir_with_parents (path, 0700) == -1 && errno != EEXIST) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Cannot get folder: %s: %s"),
			path, g_strerror (errno));
		return NULL;
	}

	return (CamelFolder *) 0xdeadbeef;
}

static CamelFolderInfo *
local_store_get_folder_info_sync (CamelStore *store,
                                  const gchar *top,
                                  CamelStoreGetFolderInfoFlags flags,
                                  GCancellable *cancellable,
                                  GError **error)
{
	/* FIXME: This is broken, but it corresponds to what was
	 * there before.
	 */

	d(printf("-- LOCAL STORE -- get folder info: %s\n", top));

	return NULL;
}

static CamelFolder *
local_store_get_inbox_folder_sync (CamelStore *store,
                                   GCancellable *cancellable,
                                   GError **error)
{
	g_set_error (
		error, CAMEL_STORE_ERROR,
		CAMEL_STORE_ERROR_NO_FOLDER,
		_("Local stores do not have an inbox"));

	return NULL;
}

static CamelFolder *
local_store_get_junk_folder_sync (CamelStore *store,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelFolder *folder;

	/* Chain up to parent's get_junk_folder_sync() method. */
	folder = CAMEL_STORE_CLASS (camel_local_store_parent_class)->
		get_junk_folder_sync (store, cancellable, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		gchar *state;

		state = camel_local_store_get_meta_path (
			CAMEL_LOCAL_STORE (store),
			CAMEL_VJUNK_NAME, ".cmeta");
		camel_object_set_state_filename (object, state);
		g_free (state);

		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static CamelFolder *
local_store_get_trash_folder_sync (CamelStore *store,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelFolder *folder;

	/* Chain up to parent's get_trash_folder_sync() method. */
	folder = CAMEL_STORE_CLASS (camel_local_store_parent_class)->
		get_trash_folder_sync (store, cancellable, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		gchar *state;

		state = camel_local_store_get_meta_path (
			CAMEL_LOCAL_STORE (store),
			CAMEL_VTRASH_NAME, ".cmeta");
		camel_object_set_state_filename (object, state);
		g_free (state);

		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static CamelFolderInfo *
local_store_create_folder_sync (CamelStore *store,
                                const gchar *parent_name,
                                const gchar *folder_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelLocalSettings *local_settings;
	CamelSettings *settings;
	CamelService *service;
	CamelFolder *folder;
	CamelFolderInfo *info = NULL;
	const gchar *path;
	gchar *name;
	struct stat st;

	service = CAMEL_SERVICE (store);
	settings = camel_service_get_settings (service);

	local_settings = CAMEL_LOCAL_SETTINGS (settings);
	path = camel_local_settings_get_path (local_settings);

	/* This is a pretty hacky version of create folder, but should basically work */

	if (!g_path_is_absolute (path)) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Store root %s is not an absolute path"), path);
		return NULL;
	}

	if (parent_name)
		name = g_strdup_printf("%s/%s/%s", path, parent_name, folder_name);
	else
		name = g_strdup_printf("%s/%s", path, folder_name);

	if (g_stat (name, &st) == 0 || errno != ENOENT) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Cannot get folder: %s: %s"),
			name, g_strerror (errno));
		g_free (name);
		return NULL;
	}

	g_free (name);

	if (parent_name)
		name = g_strdup_printf("%s/%s", parent_name, folder_name);
	else
		name = g_strdup_printf("%s", folder_name);

	folder = CAMEL_STORE_GET_CLASS (store)->get_folder_sync (
		store, name, CAMEL_STORE_FOLDER_CREATE, cancellable, error);
	if (folder) {
		g_object_unref (folder);
		info = CAMEL_STORE_GET_CLASS (store)->get_folder_info_sync (
			store, name, 0, cancellable, error);
	}

	g_free (name);

	return info;
}

/* default implementation, only delete metadata */
static gboolean
local_store_delete_folder_sync (CamelStore *store,
                                const gchar *folder_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelLocalSettings *local_settings;
	CamelSettings *settings;
	CamelService *service;
	CamelFolderInfo *fi;
	CamelFolder *lf;
	const gchar *path;
	gchar *name;
	gchar *str;

	service = CAMEL_SERVICE (store);
	settings = camel_service_get_settings (service);

	local_settings = CAMEL_LOCAL_SETTINGS (settings);
	path = camel_local_settings_get_path (local_settings);

	/* remove metadata only */
	name = g_build_filename (path, folder_name, NULL);
	str = g_strdup_printf("%s.ibex", name);
	if (camel_text_index_remove (str) == -1 && errno != ENOENT && errno != ENOTDIR) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not delete folder index file '%s': %s"),
			str, g_strerror (errno));
		g_free (str);
		g_free (name);
		return FALSE;
	}
	g_free (str);

	str = NULL;
	if ((lf = camel_store_get_folder_sync (store, folder_name, 0, cancellable, NULL))) {
		CamelObject *object = CAMEL_OBJECT (lf);
		const gchar *state_filename;

		state_filename = camel_object_get_state_filename (object);
		str = g_strdup (state_filename);

		camel_object_set_state_filename (object, NULL);

		g_object_unref (lf);
	}

	if (str == NULL)
		str = g_strdup_printf ("%s.cmeta", name);

	if (g_unlink (str) == -1 && errno != ENOENT && errno != ENOTDIR) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not delete folder meta file '%s': %s"),
			str, g_strerror (errno));
		g_free (name);
		g_free (str);
		return FALSE;
	}

	g_free (str);
	g_free (name);

	fi = camel_folder_info_new ();
	fi->full_name = g_strdup (folder_name);
	fi->display_name = g_path_get_basename (folder_name);
	fi->unread = -1;

	camel_store_folder_deleted (store, fi);
	camel_folder_info_free (fi);

	return TRUE;
}

/* default implementation, rename all */
static gboolean
local_store_rename_folder_sync (CamelStore *store,
                                const gchar *old,
                                const gchar *new,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelLocalFolder *folder;
	CamelLocalSettings *local_settings;
	CamelSettings *settings;
	CamelService *service;
	const gchar *path;
	gchar *old_basename;
	gchar *new_basename;
	gchar *newibex;
	gchar *oldibex;

	service = CAMEL_SERVICE (store);
	settings = camel_service_get_settings (service);

	local_settings = CAMEL_LOCAL_SETTINGS (settings);
	path = camel_local_settings_get_path (local_settings);

	old_basename = g_strdup_printf ("%s.ibex", old);
	new_basename = g_strdup_printf ("%s.ibex", new);

	oldibex = g_build_filename (path, old_basename, NULL);
	newibex = g_build_filename (path, new_basename, NULL);

	g_free (old_basename);
	g_free (new_basename);

	/* try to rollback failures, has obvious races */

	d(printf("local rename folder '%s' '%s'\n", old, new));

	folder = camel_object_bag_get (store->folders, old);
	if (folder && folder->index) {
		if (camel_index_rename (folder->index, newibex) == -1)
			goto ibex_failed;
	} else {
		/* TODO: camel_text_index_rename should find out if we have an active index itself? */
		if (camel_text_index_rename (oldibex, newibex) == -1)
			goto ibex_failed;
	}

	if (xrename(old, new, path, ".ev-summary", TRUE, error))
		goto summary_failed;

	if (xrename(old, new, path, ".ev-summary-meta", TRUE, error))
		goto summary_failed;

	if (xrename(old, new, path, ".cmeta", TRUE, error))
		goto cmeta_failed;

	if (xrename(old, new, path, "", FALSE, error))
		goto base_failed;

	g_free (newibex);
	g_free (oldibex);

	if (folder)
		g_object_unref (folder);

	return TRUE;

	/* The (f)utility of this recovery effort is quesitonable */

base_failed:
	xrename(new, old, path, ".cmeta", TRUE, NULL);

cmeta_failed:
	xrename(new, old, path, ".ev-summary", TRUE, NULL);
	xrename(new, old, path, ".ev-summary-meta", TRUE, NULL);
summary_failed:
	if (folder) {
		if (folder->index)
			camel_index_rename (folder->index, oldibex);
	} else
		camel_text_index_rename (newibex, oldibex);
ibex_failed:
	g_set_error (
		error, G_IO_ERROR,
		g_io_error_from_errno (errno),
		_("Could not rename '%s': %s"),
		old, g_strerror (errno));

	g_free (newibex);
	g_free (oldibex);

	if (folder)
		g_object_unref (folder);

	return FALSE;
}

static gchar *
local_store_get_full_path (CamelLocalStore *ls,
                           const gchar *full_name)
{
	CamelLocalSettings *local_settings;
	CamelSettings *settings;
	CamelService *service;
	const gchar *path;

	service = CAMEL_SERVICE (ls);
	settings = camel_service_get_settings (service);

	local_settings = CAMEL_LOCAL_SETTINGS (settings);
	path = camel_local_settings_get_path (local_settings);

	return g_build_filename (path, full_name, NULL);
}

static gchar *
local_store_get_meta_path (CamelLocalStore *ls,
                           const gchar *full_name,
                           const gchar *ext)
{
	CamelLocalSettings *local_settings;
	CamelSettings *settings;
	CamelService *service;
	const gchar *path;
	gchar *basename;
	gchar *filename;

	service = CAMEL_SERVICE (ls);
	settings = camel_service_get_settings (service);

	local_settings = CAMEL_LOCAL_SETTINGS (settings);
	path = camel_local_settings_get_path (local_settings);

	basename = g_strconcat (full_name, ext, NULL);
	filename = g_build_filename (path, basename, NULL);
	g_free (basename);

	return filename;
}

static void
camel_local_store_class_init (CamelLocalStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	g_type_class_add_private (class, sizeof (CamelLocalStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = local_store_set_property;
	object_class->get_property = local_store_get_property;
	object_class->constructed = local_store_constructed;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_LOCAL_SETTINGS;
	service_class->get_name = local_store_get_name;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->can_refresh_folder = local_store_can_refresh_folder;
	store_class->free_folder_info = camel_store_free_folder_info_full;
	store_class->get_folder_sync = local_store_get_folder_sync;
	store_class->get_folder_info_sync = local_store_get_folder_info_sync;
	store_class->get_inbox_folder_sync = local_store_get_inbox_folder_sync;
	store_class->get_junk_folder_sync = local_store_get_junk_folder_sync;
	store_class->get_trash_folder_sync = local_store_get_trash_folder_sync;
	store_class->create_folder_sync = local_store_create_folder_sync;
	store_class->delete_folder_sync = local_store_delete_folder_sync;
	store_class->rename_folder_sync = local_store_rename_folder_sync;

	class->get_full_path = local_store_get_full_path;
	class->get_meta_path = local_store_get_meta_path;

	g_object_class_install_property (
		object_class,
		PROP_NEED_SUMMARY_CHECK,
		g_param_spec_boolean (
			"need-summary-check",
			"Need Summary Check",
			"Whether to check for unexpected file changes",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_local_store_init (CamelLocalStore *local_store)
{
	local_store->priv = CAMEL_LOCAL_STORE_GET_PRIVATE (local_store);
}

/* Returns whether is this store used as 'On This Computer' main store */
gboolean
camel_local_store_is_main_store (CamelLocalStore *store)
{
	g_return_val_if_fail (store != NULL, FALSE);

	return store->is_main_store;
}

gchar *
camel_local_store_get_full_path (CamelLocalStore *store,
                                 const gchar *full_name)
{
	CamelLocalStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_LOCAL_STORE (store), NULL);
	/* XXX Guard against full_name == NULL? */

	class = CAMEL_LOCAL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_full_path != NULL, NULL);

	return class->get_full_path (store, full_name);
}

gchar *
camel_local_store_get_meta_path (CamelLocalStore *store,
                                 const gchar *full_name,
                                 const gchar *ext)
{
	CamelLocalStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_LOCAL_STORE (store), NULL);
	/* XXX Guard against full_name == NULL? */
	/* XXX Guard against ext == NULL? */

	class = CAMEL_LOCAL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_meta_path != NULL, NULL);

	return class->get_meta_path (store, full_name, ext);
}

guint32
camel_local_store_get_folder_type_by_full_name (CamelLocalStore *store,
                                                const gchar *full_name)
{
	g_return_val_if_fail (store != NULL, 0);
	g_return_val_if_fail (full_name != NULL, 0);

	if (!camel_local_store_is_main_store (store))
		return CAMEL_FOLDER_TYPE_NORMAL;

	if (g_ascii_strcasecmp (full_name, "Inbox") == 0)
		return CAMEL_FOLDER_TYPE_INBOX;
	else if (g_ascii_strcasecmp (full_name, "Outbox") == 0)
		return CAMEL_FOLDER_TYPE_OUTBOX;
	else if (g_ascii_strcasecmp (full_name, "Sent") == 0)
		return CAMEL_FOLDER_TYPE_SENT;

	return CAMEL_FOLDER_TYPE_NORMAL;
}

/**
 * camel_local_store_get_need_summary_check:
 * @store: a #CamelLocalStore
 *
 * Returns whether local mail files for @store should be check for
 * consistency and the summary database synchronized with them.  This
 * is necessary to handle another mail application altering the files,
 * such as local mail delivery using fetchmail.
 *
 * Returns: whether to check for changes in local mail files
 *
 * Since: 3.2
 **/
gboolean
camel_local_store_get_need_summary_check (CamelLocalStore *store)
{
	g_return_val_if_fail (CAMEL_IS_LOCAL_STORE (store), FALSE);

	return store->priv->need_summary_check;
}

/**
 * camel_local_store_set_need_summary_check:
 * @store: a #CamelLocalStore
 * @need_summary_check: whether to check for changes in local mail files
 *
 * Sets whether local mail files for @store should be checked for
 * consistency and the summary database synchronized with them.  This
 * is necessary to handle another mail application altering the files,
 * such as local mail delivery using fetchmail.
 *
 * Since: 3.2
 **/
void
camel_local_store_set_need_summary_check (CamelLocalStore *store,
                                          gboolean need_summary_check)
{
	g_return_if_fail (CAMEL_IS_LOCAL_STORE (store));

	store->priv->need_summary_check = need_summary_check;

	g_object_notify (G_OBJECT (store), "need-summary-check");
}

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

static gboolean construct (CamelService *service, CamelSession *session, CamelProvider *provider, CamelURL *url, GError **error);
static CamelFolder *get_folder(CamelStore *store, const gchar *folder_name, guint32 flags, GError **error);
static gchar *get_name(CamelService *service, gboolean brief);
static CamelFolder *local_get_inbox (CamelStore *store, GError **error);
static CamelFolder *local_get_junk(CamelStore *store, GError **error);
static CamelFolder *local_get_trash(CamelStore *store, GError **error);
static CamelFolderInfo *get_folder_info (CamelStore *store, const gchar *top, guint32 flags, GError **error);
static gboolean delete_folder(CamelStore *store, const gchar *folder_name, GError **error);
static gboolean rename_folder(CamelStore *store, const gchar *old, const gchar *new, GError **error);
static CamelFolderInfo *create_folder(CamelStore *store, const gchar *parent_name, const gchar *folder_name, GError **error);
static gboolean local_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, GError **error);

static gchar *local_get_full_path(CamelLocalStore *lf, const gchar *full_name);
static gchar *local_get_meta_path(CamelLocalStore *lf, const gchar *full_name, const gchar *ext);

G_DEFINE_TYPE (CamelLocalStore, camel_local_store, CAMEL_TYPE_STORE)

static void
local_store_finalize (GObject *object)
{
	CamelLocalStore *local_store = CAMEL_LOCAL_STORE (object);

	g_free (local_store->toplevel_dir);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_local_store_parent_class)->finalize (object);
}

static void
camel_local_store_class_init (CamelLocalStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = local_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = construct;
	service_class->get_name = get_name;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder = get_folder;
	store_class->get_inbox = local_get_inbox;
	store_class->get_trash = local_get_trash;
	store_class->get_junk = local_get_junk;
	store_class->get_folder_info = get_folder_info;
	store_class->free_folder_info = camel_store_free_folder_info_full;
	store_class->create_folder = create_folder;
	store_class->delete_folder = delete_folder;
	store_class->rename_folder = rename_folder;
	store_class->can_refresh_folder = local_can_refresh_folder;

	class->get_full_path = local_get_full_path;
	class->get_meta_path = local_get_meta_path;
}

static void
camel_local_store_init (CamelLocalStore *local_store)
{
}

static gboolean
construct (CamelService *service,
           CamelSession *session,
           CamelProvider *provider,
           CamelURL *url,
           GError **error)
{
	CamelLocalStore *local_store = CAMEL_LOCAL_STORE (service);
	CamelServiceClass *service_class;
	gint len;
	gchar *local_store_path, *local_store_uri;

	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_local_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	len = strlen (service->url->path);
	if (!G_IS_DIR_SEPARATOR (service->url->path[len - 1]))
		local_store->toplevel_dir = g_strdup_printf ("%s/", service->url->path);
	else
		local_store->toplevel_dir = g_strdup (service->url->path);

	local_store->is_main_store = FALSE;

	local_store_path = g_build_filename (e_get_user_data_dir (), "mail", "local", NULL);
	local_store_uri = g_filename_to_uri (local_store_path, NULL, NULL);
	if (local_store_uri) {
		CamelProvider *provider = service->provider;
		CamelURL *local_store_url = camel_url_new (local_store_uri, NULL);

		camel_url_set_protocol (local_store_url, service->url->protocol);
		camel_url_set_host (local_store_url, service->url->host);

		local_store->is_main_store = (provider && provider->url_equal) ? provider->url_equal (service->url, local_store_url) : camel_url_equal (service->url, local_store_url);
		camel_url_free (local_store_url);
	}

	g_free (local_store_uri);
	g_free (local_store_path);

	return TRUE;
}

const gchar *
camel_local_store_get_toplevel_dir (CamelLocalStore *store)
{
	return store->toplevel_dir;
}

static CamelFolder *
get_folder(CamelStore *store, const gchar *folder_name, guint32 flags, GError **error)
{
	gint len = strlen(((CamelLocalStore *)store)->toplevel_dir);
	gchar *path = g_alloca(len + 1);
	struct stat st;

	strcpy(path, ((CamelLocalStore *)store)->toplevel_dir);
	if (G_IS_DIR_SEPARATOR(path[len-1]))
		path[len-1] = '\0';

	if (!g_path_is_absolute(path)) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Store root %s is not an absolute path"), path);
		return NULL;
	}

	if (g_stat(path, &st) == 0) {
		if (!S_ISDIR(st.st_mode)) {
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

static CamelFolder *
local_get_inbox(CamelStore *store, GError **error)
{
	g_set_error (
		error, CAMEL_STORE_ERROR,
		CAMEL_STORE_ERROR_NO_FOLDER,
		_("Local stores do not have an inbox"));

	return NULL;
}

static CamelFolder *
local_get_trash (CamelStore *store,
                 GError **error)
{
	CamelFolder *folder;

	/* Chain up to parent's get_trash() method. */
	folder = CAMEL_STORE_CLASS (camel_local_store_parent_class)->get_trash (store, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		gchar *state = camel_local_store_get_meta_path(store, CAMEL_VTRASH_NAME, ".cmeta");

		camel_object_set_state_filename (object, state);
		g_free(state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static CamelFolder *
local_get_junk (CamelStore *store,
                GError **error)
{
	CamelFolder *folder;

	/* Chain up to parent's get_junk() method. */
	folder = CAMEL_STORE_CLASS (camel_local_store_parent_class)->get_junk (store, error);

	if (folder) {
		CamelObject *object = CAMEL_OBJECT (folder);
		gchar *state = camel_local_store_get_meta_path(store, CAMEL_VJUNK_NAME, ".cmeta");

		camel_object_set_state_filename (object, state);
		g_free(state);
		/* no defaults? */
		camel_object_state_read (object);
	}

	return folder;
}

static gchar *
get_name (CamelService *service, gboolean brief)
{
	gchar *dir = ((CamelLocalStore*)service)->toplevel_dir;

	if (brief)
		return g_strdup (dir);
	else
		return g_strdup_printf (_("Local mail file %s"), dir);
}

static CamelFolderInfo *
get_folder_info (CamelStore *store, const gchar *top,
		 guint32 flags, GError **error)
{
	/* FIXME: This is broken, but it corresponds to what was
	 * there before.
	 */

	d(printf("-- LOCAL STORE -- get folder info: %s\n", top));

	return NULL;
}

static CamelFolderInfo *
create_folder (CamelStore *store,
               const gchar *parent_name,
               const gchar *folder_name,
               GError **error)
{
	gchar *path = ((CamelLocalStore *)store)->toplevel_dir;
	gchar *name;
	CamelFolder *folder;
	CamelFolderInfo *info = NULL;
	struct stat st;

	/* This is a pretty hacky version of create folder, but should basically work */

	if (!g_path_is_absolute(path)) {
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

	if (g_stat(name, &st) == 0 || errno != ENOENT) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Cannot get folder: %s: %s"),
			name, g_strerror (errno));
		g_free(name);
		return NULL;
	}

	g_free(name);

	if (parent_name)
		name = g_strdup_printf("%s/%s", parent_name, folder_name);
	else
		name = g_strdup_printf("%s", folder_name);

	folder = CAMEL_STORE_GET_CLASS (store)->get_folder (
		store, name, CAMEL_STORE_FOLDER_CREATE, error);
	if (folder) {
		g_object_unref (folder);
		info = CAMEL_STORE_GET_CLASS (store)->get_folder_info (
			store, name, 0, error);
	}

	g_free(name);

	return info;
}

static gint
xrename (const gchar *oldp,
         const gchar *newp,
         const gchar *prefix,
         const gchar *suffix,
         gint missingok,
         GError **error)
{
	struct stat st;
	gchar *old = g_strconcat(prefix, oldp, suffix, NULL);
	gchar *new = g_strconcat(prefix, newp, suffix, NULL);
	gint ret = -1;
	gint err = 0;

	d(printf("renaming %s%s to %s%s\n", oldp, suffix, newp, suffix));

	if (g_stat(old, &st) == -1) {
		if (missingok && errno == ENOENT) {
			ret = 0;
		} else {
			err = errno;
			ret = -1;
		}
	} else if ((!g_file_test (new, G_FILE_TEST_EXISTS) || g_remove (new) == 0) &&
		   g_rename(old, new) == 0) {
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

	g_free(old);
	g_free(new);
	return ret;
}

/* default implementation, rename all */
static gboolean
rename_folder(CamelStore *store,
              const gchar *old,
              const gchar *new,
              GError **error)
{
	gchar *path = CAMEL_LOCAL_STORE (store)->toplevel_dir;
	CamelLocalFolder *folder = NULL;
	gchar *newibex = g_strdup_printf("%s%s.ibex", path, new);
	gchar *oldibex = g_strdup_printf("%s%s.ibex", path, old);

	/* try to rollback failures, has obvious races */

	d(printf("local rename folder '%s' '%s'\n", old, new));

	folder = camel_object_bag_get(store->folders, old);
	if (folder && folder->index) {
		if (camel_index_rename(folder->index, newibex) == -1)
			goto ibex_failed;
	} else {
		/* TODO: camel_text_index_rename should find out if we have an active index itself? */
		if (camel_text_index_rename(oldibex, newibex) == -1)
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

	g_free(newibex);
	g_free(oldibex);

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
			camel_index_rename(folder->index, oldibex);
	} else
		camel_text_index_rename(newibex, oldibex);
ibex_failed:
	g_set_error (
		error, G_IO_ERROR,
		g_io_error_from_errno (errno),
		_("Could not rename '%s': %s"),
		old, g_strerror (errno));

	g_free(newibex);
	g_free(oldibex);

	if (folder)
		g_object_unref (folder);

	return FALSE;
}

/* default implementation, only delete metadata */
static gboolean
delete_folder (CamelStore *store,
               const gchar *folder_name,
               GError **error)
{
	CamelFolderInfo *fi;
	CamelFolder *lf;
	gchar *name;
	gchar *str;

	/* remove metadata only */
	name = g_strdup_printf("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);
	str = g_strdup_printf("%s.ibex", name);
	if (camel_text_index_remove(str) == -1 && errno != ENOENT) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not delete folder index file '%s': %s"),
			str, g_strerror (errno));
		g_free(str);
		g_free (name);
		return FALSE;
	}
	g_free(str);

	str = NULL;
	if ((lf = camel_store_get_folder (store, folder_name, 0, NULL))) {
		CamelObject *object = CAMEL_OBJECT (lf);
		const gchar *state_filename;

		state_filename = camel_object_get_state_filename (object);
		str = g_strdup (state_filename);

		camel_object_set_state_filename (object, NULL);

		g_object_unref (lf);
	}

	if (str == NULL)
		str = g_strdup_printf ("%s.cmeta", name);

	if (g_unlink (str) == -1 && errno != ENOENT) {
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
	fi->name = g_path_get_basename (folder_name);
	fi->uri = g_strdup_printf ("%s:%s#%s", ((CamelService *) store)->url->protocol,
				   CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);
	fi->unread = -1;

	camel_store_folder_deleted (store, fi);
	camel_folder_info_free (fi);

	return TRUE;
}

static gchar *
local_get_full_path(CamelLocalStore *ls, const gchar *full_name)
{
	return g_strdup_printf("%s%s", ls->toplevel_dir, full_name);
}

static gchar *
local_get_meta_path(CamelLocalStore *ls, const gchar *full_name, const gchar *ext)
{
	return g_strdup_printf("%s%s%s", ls->toplevel_dir, full_name, ext);
}

static gboolean
local_can_refresh_folder (CamelStore *store, CamelFolderInfo *info, GError **error)
{
	/* any local folder can be refreshed */
	return TRUE;
}

/* Returns whether is this store used as 'On This Computer' main store */
gboolean
camel_local_store_is_main_store (CamelLocalStore *store)
{
	g_return_val_if_fail (store != NULL, FALSE);

	return store->is_main_store;
}

guint32
camel_local_store_get_folder_type_by_full_name (CamelLocalStore *store, const gchar *full_name)
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

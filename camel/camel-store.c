/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-store.c : Abstract class for an email store */

/*
 * Authors:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *  Dan Winship <danw@ximian.com>
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

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-folder.h"
#include "camel-marshal.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-vtrash-folder.h"

#define d(x)
#define w(x)

#define CAMEL_STORE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_STORE, CamelStorePrivate))

struct _CamelStorePrivate {
	GStaticRecMutex folder_lock;	/* for locking folder operations */
};

enum {
	FOLDER_CREATED,
	FOLDER_DELETED,
	FOLDER_OPENED,
	FOLDER_RENAMED,
	FOLDER_SUBSCRIBED,
	FOLDER_UNSUBSCRIBED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_ABSTRACT_TYPE (CamelStore, camel_store, CAMEL_TYPE_SERVICE)

/**
 * ignore_no_such_table_exception:
 * Clears the exception 'ex' when it's the 'no such table' exception.
 **/
static void
ignore_no_such_table_exception (GError **error)
{
	if (error == NULL || *error == NULL)
		return;

	if (g_ascii_strncasecmp ((*error)->message, "no such table", 13) == 0)
		g_clear_error (error);
}

static CamelFolder *
store_get_special (CamelStore *store,
                   camel_vtrash_folder_t type)
{
	CamelFolder *folder;
	GPtrArray *folders;
	gint i;

	folder = camel_vtrash_folder_new(store, type);
	folders = camel_object_bag_list(store->folders);
	for (i=0;i<folders->len;i++) {
		if (!CAMEL_IS_VTRASH_FOLDER(folders->pdata[i]))
			camel_vee_folder_add_folder((CamelVeeFolder *)folder, (CamelFolder *)folders->pdata[i]);
		g_object_unref (folders->pdata[i]);
	}
	g_ptr_array_free(folders, TRUE);

	return folder;
}

static void
store_finalize (GObject *object)
{
	CamelStore *store = CAMEL_STORE (object);

	if (store->folders != NULL)
		camel_object_bag_destroy (store->folders);

	g_static_rec_mutex_free (&store->priv->folder_lock);

	if (store->cdb_r != NULL) {
		camel_db_close (store->cdb_r);
		store->cdb_r = NULL;
	}

	if (store->cdb_w != NULL) {
		camel_db_close (store->cdb_w);
		store->cdb_w = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_store_parent_class)->finalize (object);
}

static void
store_constructed (GObject *object)
{
	CamelStore *store;
	CamelStoreClass *class;

	store = CAMEL_STORE (object);
	class = CAMEL_STORE_GET_CLASS (store);

	if (class->hash_folder_name != NULL)
		store->folders = camel_object_bag_new (
			class->hash_folder_name,
			class->compare_folder_name,
			(CamelCopyFunc) g_strdup, g_free);
	else
		store->folders = NULL;
}

static gboolean
store_construct (CamelService *service,
                 CamelSession *session,
                 CamelProvider *provider,
                 CamelURL *url,
                 GError **error)
{
	CamelServiceClass *service_class;
	CamelStore *store = CAMEL_STORE(service);
	gchar *store_db_path, *store_path = NULL;

	/* Chain up to parent's construct() method. */
	service_class = CAMEL_SERVICE_CLASS (camel_store_parent_class);
	if (!service_class->construct (service, session, provider, url, error))
		return FALSE;

	store_db_path = g_build_filename (service->url->path, CAMEL_DB_FILE, NULL);

	if (!service->url->path || strlen (store_db_path) < 2) {
		store_path = camel_session_get_storage_path (session, service, error);

		g_free (store_db_path);
		store_db_path = g_build_filename (store_path, CAMEL_DB_FILE, NULL);
	}

	if (!g_file_test (service->url->path ? service->url->path : store_path, G_FILE_TEST_EXISTS)) {
		/* Cache might be blown. Recreate. */
		g_mkdir_with_parents (service->url->path ? service->url->path : store_path, S_IRWXU);
	}

	g_free (store_path);

	/* This is for reading from the store */
	store->cdb_r = camel_db_open (store_db_path, NULL);
	if (camel_debug("sqlite"))
		printf("store_db_path %s\n", store_db_path);
	if (store->cdb_r == NULL) {
		gchar *store_path;

		if (camel_debug("sqlite"))
			g_print ("Failure for store_db_path : [%s]\n", store_db_path);
		g_free (store_db_path);

		store_path =  camel_session_get_storage_path (session, service, NULL);
		store_db_path = g_build_filename (store_path, CAMEL_DB_FILE, NULL);
		g_free (store_path);

		store->cdb_r = camel_db_open (store_db_path, NULL);
		if (store->cdb_r == NULL) {
			g_print("Retry with %s failed\n", store_db_path);
			g_free(store_db_path);
			return FALSE;
		}
	}
	g_free (store_db_path);

	if (camel_db_create_folders_table (store->cdb_r, error)) {
		g_warning ("something went wrong terribly during db creation \n");
		return FALSE;
	}

	/* This is for writing to the store */
	store->cdb_w = camel_db_clone (store->cdb_r, error);

	if (camel_url_get_param(url, "filter"))
		store->flags |= CAMEL_STORE_FILTER_INBOX;

	return TRUE;
}

static CamelFolder *
store_get_inbox (CamelStore *store,
                 GError **error)
{
	CamelStoreClass *class;
	CamelFolder *folder;

	/* Assume the inbox's name is "inbox" and open with default flags. */
	class = CAMEL_STORE_GET_CLASS (store);
	folder = class->get_folder (store, "inbox", 0, error);
	CAMEL_CHECK_GERROR (store, get_folder, folder != NULL, error);

	return folder;
}

static CamelFolder *
store_get_trash (CamelStore *store,
                 GError **error)
{
	return store_get_special (store, CAMEL_VTRASH_FOLDER_TRASH);
}

static CamelFolder *
store_get_junk (CamelStore *store,
                GError **error)
{
	return store_get_special (store, CAMEL_VTRASH_FOLDER_JUNK);
}

static gboolean
store_sync (CamelStore *store,
            gint expunge,
            GError **error)
{
	GPtrArray *folders;
	CamelFolder *folder;
	gboolean success = TRUE;
	gint i;
	GError *local_error = NULL;

	if (store->folders == NULL)
		return TRUE;

	/* We don't sync any vFolders, that is used to update certain
	 * vfolder queries mainly, and we're really only interested in
	 * storing/expunging the physical mails. */
	folders = camel_object_bag_list(store->folders);
	for (i=0;i<folders->len;i++) {
		folder = folders->pdata[i];
		if (!CAMEL_IS_VEE_FOLDER(folder)
		    && local_error == NULL) {
			camel_folder_sync(folder, expunge, &local_error);
			ignore_no_such_table_exception (&local_error);
		} else if (CAMEL_IS_VEE_FOLDER(folder))
			camel_vee_folder_sync_headers(folder, NULL); /* Literally don't care of vfolder exceptions */
		g_object_unref (folder);
	}

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		success = FALSE;
	}

	g_ptr_array_free (folders, TRUE);

	return success;
}

static gboolean
store_noop (CamelStore *store,
            GError **error)
{
	return TRUE;
}

static gboolean
store_can_refresh_folder (CamelStore *store,
                          CamelFolderInfo *info,
                          GError **error)
{
	return ((info->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_INBOX);
}

static void
camel_store_class_init (CamelStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;

	g_type_class_add_private (class, sizeof (CamelStorePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = store_finalize;
	object_class->constructed = store_constructed;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->construct = store_construct;

	class->hash_folder_name = g_str_hash;
	class->compare_folder_name = g_str_equal;
	class->get_inbox = store_get_inbox;
	class->get_trash = store_get_trash;
	class->get_junk = store_get_junk;
	class->sync = store_sync;
	class->noop = store_noop;
	class->can_refresh_folder = store_can_refresh_folder;

	signals[FOLDER_CREATED] = g_signal_new (
		"folder-created",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_created),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[FOLDER_DELETED] = g_signal_new (
		"folder-deleted",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_deleted),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[FOLDER_OPENED] = g_signal_new (
		"folder-opened",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_opened),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		CAMEL_TYPE_FOLDER);

	signals[FOLDER_RENAMED] = g_signal_new (
		"folder-renamed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_renamed),
		NULL, NULL,
		camel_marshal_VOID__STRING_POINTER,
		G_TYPE_NONE, 2,
		G_TYPE_STRING,
		G_TYPE_POINTER);

	signals[FOLDER_SUBSCRIBED] = g_signal_new (
		"folder-subscribed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_subscribed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);

	signals[FOLDER_UNSUBSCRIBED] = g_signal_new (
		"folder-unsubscribed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (CamelStoreClass, folder_unsubscribed),
		NULL, NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);
}

static void
camel_store_init (CamelStore *store)
{
	store->priv = CAMEL_STORE_GET_PRIVATE (store);

	/* set vtrash and vjunk on by default */
	store->flags = CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK;
	store->mode = CAMEL_STORE_READ | CAMEL_STORE_WRITE;

	g_static_rec_mutex_init (&store->priv->folder_lock);
}

GQuark
camel_store_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "camel-store-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

/**
 * camel_store_get_folder:
 * @store: a #CamelStore object
 * @folder_name: name of the folder to get
 * @flags: folder flags (create, save body index, etc)
 * @error: return location for a #GError, or %NULL
 *
 * Get a specific folder object from the store by name.
 *
 * Returns: the folder corresponding to the path @folder_name or %NULL.
 **/
CamelFolder *
camel_store_get_folder (CamelStore *store,
                        const gchar *folder_name,
                        guint32 flags,
                        GError **error)
{
	CamelStoreClass *class;
	CamelFolder *folder = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);
	g_return_val_if_fail (folder_name != NULL, NULL);

	class = CAMEL_STORE_GET_CLASS (store);

	/* O_EXCL doesn't make sense if we aren't requesting to also create the folder if it doesn't exist */
	if (!(flags & CAMEL_STORE_FOLDER_CREATE))
		flags &= ~CAMEL_STORE_FOLDER_EXCL;

	if (store->folders) {
		/* Try cache first. */
		folder = camel_object_bag_reserve(store->folders, folder_name);
		if (folder && (flags & CAMEL_STORE_FOLDER_EXCL)) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Cannot create folder '%s': folder exists"),
				folder_name);
                        camel_object_bag_abort (store->folders, folder_name);
			g_object_unref (folder);
			return NULL;
		}
	}

	if (!folder) {

		if (flags & CAMEL_STORE_IS_MIGRATING) {
				if ((store->flags & CAMEL_STORE_VTRASH) && strcmp(folder_name, CAMEL_VTRASH_NAME) == 0) {
						if (store->folders)
								camel_object_bag_abort(store->folders, folder_name);
						return NULL;
				}

				if ((store->flags & CAMEL_STORE_VJUNK) && strcmp(folder_name, CAMEL_VJUNK_NAME) == 0) {
						if (store->folders)
								camel_object_bag_abort(store->folders, folder_name);
						return NULL;
				}
		}

		if ((store->flags & CAMEL_STORE_VTRASH) && strcmp(folder_name, CAMEL_VTRASH_NAME) == 0) {
			folder = class->get_trash(store, error);
			CAMEL_CHECK_GERROR (store, get_trash, folder != NULL, error);
		} else if ((store->flags & CAMEL_STORE_VJUNK) && strcmp(folder_name, CAMEL_VJUNK_NAME) == 0) {
			folder = class->get_junk(store, error);
			CAMEL_CHECK_GERROR (store, get_junk, folder != NULL, error);
		} else {
			folder = class->get_folder(store, folder_name, flags, error);
			CAMEL_CHECK_GERROR (store, get_folder, folder != NULL, error);

			if (folder) {
				CamelVeeFolder *vfolder;

				if ((store->flags & CAMEL_STORE_VTRASH)
				    && (vfolder = camel_object_bag_get(store->folders, CAMEL_VTRASH_NAME))) {
					camel_vee_folder_add_folder(vfolder, folder);
					g_object_unref (vfolder);
				}

				if ((store->flags & CAMEL_STORE_VJUNK)
				    && (vfolder = camel_object_bag_get(store->folders, CAMEL_VJUNK_NAME))) {
					camel_vee_folder_add_folder(vfolder, folder);
					g_object_unref (vfolder);
				}
			}
		}

		if (store->folders) {
			if (folder)
				camel_object_bag_add(store->folders, folder_name, folder);
			else
				camel_object_bag_abort(store->folders, folder_name);
		}

		if (folder)
			g_signal_emit (store, signals[FOLDER_OPENED], 0, folder);
	}

	return folder;
}

/**
 * camel_store_create_folder:
 * @store: a #CamelStore object
 * @parent_name: name of the new folder's parent, or %NULL
 * @folder_name: name of the folder to create
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new folder as a child of an existing folder.
 * @parent_name can be %NULL to create a new top-level folder.
 *
 * Returns: info about the created folder, which the caller must
 * free with #camel_store_free_folder_info, or %NULL.
 **/
CamelFolderInfo *
camel_store_create_folder (CamelStore *store,
                           const gchar *parent_name,
                           const gchar *folder_name,
                           GError **error)
{
	CamelStoreClass *class;
	CamelFolderInfo *fi;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);
	g_return_val_if_fail (folder_name != NULL, NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->create_folder != NULL, NULL);

	if ((parent_name == NULL || parent_name[0] == 0)
	    && (((store->flags & CAMEL_STORE_VTRASH) && strcmp(folder_name, CAMEL_VTRASH_NAME) == 0)
		|| ((store->flags & CAMEL_STORE_VJUNK) && strcmp(folder_name, CAMEL_VJUNK_NAME) == 0))) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("Cannot create folder: %s: folder exists"),
			folder_name);
		return NULL;
	}

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);
	fi = class->create_folder (store, parent_name, folder_name, error);
	CAMEL_CHECK_GERROR (store, create_folder, fi != NULL, error);
	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return fi;
}

/* deletes folder/removes it from the folder cache, if it's there */
static void
cs_delete_cached_folder(CamelStore *store, const gchar *folder_name)
{
	CamelFolder *folder;

	if (store->folders
	    && (folder = camel_object_bag_get(store->folders, folder_name))) {
		CamelVeeFolder *vfolder;

		if ((store->flags & CAMEL_STORE_VTRASH)
		    && (vfolder = camel_object_bag_get(store->folders, CAMEL_VTRASH_NAME))) {
			camel_vee_folder_remove_folder(vfolder, folder);
			g_object_unref (vfolder);
		}

		if ((store->flags & CAMEL_STORE_VJUNK)
		    && (vfolder = camel_object_bag_get(store->folders, CAMEL_VJUNK_NAME))) {
			camel_vee_folder_remove_folder(vfolder, folder);
			g_object_unref (vfolder);
		}

		camel_folder_delete(folder);

		camel_object_bag_remove(store->folders, folder);
		g_object_unref (folder);
	}
}

/**
 * camel_store_delete_folder:
 * @store: a #CamelStore object
 * @folder_name: name of the folder to delete
 * @error: return location for a #GError, or %NULL
 *
 * Deletes the named folder. The folder must be empty.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_store_delete_folder (CamelStore *store,
                           const gchar *folder_name,
                           GError **error)
{
	CamelStoreClass *class;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->delete_folder != NULL, FALSE);

	/* TODO: should probably be a parameter/bit on the storeinfo */
	if (((store->flags & CAMEL_STORE_VTRASH) && strcmp(folder_name, CAMEL_VTRASH_NAME) == 0)
	    || ((store->flags & CAMEL_STORE_VJUNK) && strcmp(folder_name, CAMEL_VJUNK_NAME) == 0)) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot delete folder: %s: Invalid operation"),
			folder_name);
		return FALSE;
	}

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	success = class->delete_folder (store, folder_name, &local_error);
	CAMEL_CHECK_GERROR (store, delete_folder, success, &local_error);

	/* ignore 'no such table' errors */
	if (local_error != NULL &&
	    g_ascii_strncasecmp (local_error->message, "no such table", 13) == 0)
		g_clear_error (&local_error);

	if (local_error == NULL)
		cs_delete_cached_folder(store, folder_name);
	else
		g_propagate_error (error, local_error);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return success;
}

/**
 * camel_store_rename_folder:
 * @store: a #CamelStore object
 * @old_namein: the current name of the folder
 * @new_name: the new name of the folder
 * @error: return location for a #GError, or %NULL
 *
 * Rename a named folder to a new name.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_store_rename_folder (CamelStore *store,
                           const gchar *old_namein,
                           const gchar *new_name,
                           GError **error)
{
	CamelStoreClass *class;
	CamelFolder *folder;
	gint i, oldlen, namelen;
	GPtrArray *folders = NULL;
	gchar *old_name;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (old_namein != NULL, FALSE);
	g_return_val_if_fail (new_name != NULL, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->rename_folder != NULL, FALSE);

	if (strcmp(old_namein, new_name) == 0)
		return TRUE;

	if (((store->flags & CAMEL_STORE_VTRASH) && strcmp(old_namein, CAMEL_VTRASH_NAME) == 0)
	    || ((store->flags & CAMEL_STORE_VJUNK) && strcmp(old_namein, CAMEL_VJUNK_NAME) == 0)) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("Cannot rename folder: %s: Invalid operation"),
			old_namein);
		return FALSE;
	}

	/* need to save this, since old_namein might be folder->full_name, which could go away */
	old_name = g_strdup(old_namein);
	oldlen = strlen(old_name);

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	/* If the folder is open (or any subfolders of the open folder)
	   We need to rename them atomically with renaming the actual folder path */
	if (store->folders) {
		folders = camel_object_bag_list(store->folders);
		for (i=0;i<folders->len;i++) {
			const gchar *full_name;

			folder = folders->pdata[i];
			full_name = camel_folder_get_full_name (folder);

			namelen = strlen (full_name);
			if ((namelen == oldlen &&
			     strcmp (full_name, old_name) == 0)
			    || ((namelen > oldlen)
				&& strncmp (full_name, old_name, oldlen) == 0
				&& full_name[oldlen] == '/')) {
				camel_folder_lock (folder, CAMEL_FOLDER_REC_LOCK);
			} else {
				g_ptr_array_remove_index_fast(folders, i);
				i--;
				g_object_unref (folder);
			}
		}
	}

	/* Now try the real rename (will emit renamed signal) */
	success = class->rename_folder (store, old_name, new_name, error);
	CAMEL_CHECK_GERROR (store, rename_folder, success, error);

	/* If it worked, update all open folders/unlock them */
	if (folders) {
		if (success) {
			guint32 flags = CAMEL_STORE_FOLDER_INFO_RECURSIVE;
			CamelFolderInfo *folder_info;

			for (i=0;i<folders->len;i++) {
				const gchar *full_name;
				gchar *new;

				folder = folders->pdata[i];
				full_name = camel_folder_get_full_name (folder);

				new = g_strdup_printf("%s%s", new_name, full_name + strlen(old_name));
				camel_object_bag_rekey(store->folders, folder, new);
				camel_folder_rename(folder, new);
				g_free(new);

				camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
				g_object_unref (folder);
			}

			/* Emit renamed signal */
			if (store->flags & CAMEL_STORE_SUBSCRIPTIONS)
				flags |= CAMEL_STORE_FOLDER_INFO_SUBSCRIBED;

			folder_info = class->get_folder_info(store, new_name, flags, error);
			CAMEL_CHECK_GERROR (store, get_folder_info, folder_info != NULL, error);

			if (folder_info != NULL) {
				camel_store_folder_renamed (store, old_name, folder_info);
				class->free_folder_info (store, folder_info);
			}
		} else {
			/* Failed, just unlock our folders for re-use */
			for (i=0;i<folders->len;i++) {
				folder = folders->pdata[i];
				camel_folder_unlock (folder, CAMEL_FOLDER_REC_LOCK);
				g_object_unref (folder);
			}
		}
	}

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	g_ptr_array_free(folders, TRUE);
	g_free(old_name);

	return success;
}

/**
 * camel_store_folder_created:
 * @store: a #CamelStore
 * @info: information about the created folder
 *
 * Emits the #CamelStore::folder-created signal.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 2.32
 **/
void
camel_store_folder_created (CamelStore *store,
                            CamelFolderInfo *info)
{
	g_return_if_fail (CAMEL_STORE (store));
	g_return_if_fail (info != NULL);

	g_signal_emit (store, signals[FOLDER_CREATED], 0, info);
}

/**
 * camel_store_folder_deleted:
 * @store: a #CamelStore
 * @info: information about the deleted folder
 *
 * Emits the #CamelStore::folder-deleted signal.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 2.32
 **/
void
camel_store_folder_deleted (CamelStore *store,
                            CamelFolderInfo *info)
{
	g_return_if_fail (CAMEL_STORE (store));
	g_return_if_fail (info != NULL);

	g_signal_emit (store, signals[FOLDER_DELETED], 0, info);
}

/**
 * camel_store_folder_renamed:
 * @store: a #CamelStore
 * @old_name: the old name of the folder
 * @info: information about the renamed folder
 *
 * Emits the #CamelStore::folder-renamed signal.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 2.32
 **/
void
camel_store_folder_renamed (CamelStore *store,
                            const gchar *old_name,
                            CamelFolderInfo *info)
{
	g_return_if_fail (CAMEL_STORE (store));
	g_return_if_fail (old_name != NULL);
	g_return_if_fail (info != NULL);

	g_signal_emit (store, signals[FOLDER_RENAMED], 0, old_name, info);
}

/**
 * camel_store_folder_subscribed:
 * @store: a #CamelStore
 * @info: information about the subscribed folder
 *
 * Emits the #CamelStore::folder-subscribed signal.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 2.32
 **/
void
camel_store_folder_subscribed (CamelStore *store,
                               CamelFolderInfo *info)
{
	g_return_if_fail (CAMEL_STORE (store));
	g_return_if_fail (info != NULL);

	g_signal_emit (store, signals[FOLDER_SUBSCRIBED], 0, info);
}

/**
 * camel_store_folder_unsubscribed:
 * @store: a #CamelStore
 * @info: information about the unsubscribed folder
 *
 * Emits the #CamelStore::folder-unsubscribed signal.
 *
 * This function is only intended for Camel providers.
 *
 * Since: 2.32
 **/
void
camel_store_folder_unsubscribed (CamelStore *store,
                                 CamelFolderInfo *info)
{
	g_return_if_fail (CAMEL_STORE (store));
	g_return_if_fail (info != NULL);

	g_signal_emit (store, signals[FOLDER_UNSUBSCRIBED], 0, info);
}

/**
 * camel_store_get_inbox:
 * @store: a #CamelStore object
 * @error: return location for a #GError, or %NULL
 *
 * Returns: the folder in the store into which new mail is delivered,
 * or %NULL if no such folder exists.
 **/
CamelFolder *
camel_store_get_inbox (CamelStore *store,
                       GError **error)
{
	CamelStoreClass *class;
	CamelFolder *folder;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_inbox != NULL, NULL);

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	folder = class->get_inbox (store, error);
	CAMEL_CHECK_GERROR (store, get_inbox, folder != NULL, error);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return folder;
}

/**
 * camel_store_get_trash:
 * @store: a #CamelStore object
 * @error: return location for a #GError, or %NULL
 *
 * Returns: the folder in the store into which trash is delivered, or
 * %NULL if no such folder exists.
 **/
CamelFolder *
camel_store_get_trash (CamelStore *store,
                       GError **error)
{
	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

	if ((store->flags & CAMEL_STORE_VTRASH) == 0) {
		CamelStoreClass *class;
		CamelFolder *folder;

		class = CAMEL_STORE_GET_CLASS (store);
		g_return_val_if_fail (class->get_trash != NULL, NULL);

		folder = class->get_trash (store, error);
		CAMEL_CHECK_GERROR (store, get_trash, folder != NULL, error);

		return folder;
	}

	return camel_store_get_folder (store, CAMEL_VTRASH_NAME, 0, error);
}

/**
 * camel_store_get_junk:
 * @store: a #CamelStore object
 * @error: return location for a #GError, or %NULL
 *
 * Returns: the folder in the store into which junk is delivered, or
 * %NULL if no such folder exists.
 **/
CamelFolder *
camel_store_get_junk (CamelStore *store,
                      GError **error)
{
	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

	if ((store->flags & CAMEL_STORE_VJUNK) == 0) {
		CamelStoreClass *class;
		CamelFolder *folder;

		class = CAMEL_STORE_GET_CLASS (store);
		g_return_val_if_fail (class->get_junk != NULL, NULL);

		folder = class->get_junk (store, error);
		CAMEL_CHECK_GERROR (store, get_junk, folder != NULL, error);

		return folder;
	}

	return camel_store_get_folder (store, CAMEL_VJUNK_NAME, 0, error);
}

/**
 * camel_store_sync:
 * @store: a #CamelStore object
 * @expunge: %TRUE if an expunge should be done after sync or %FALSE otherwise
 * @error: return location for a #GError, or %NULL
 *
 * Syncs any changes that have been made to the store object and its
 * folders with the real store.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_store_sync (CamelStore *store,
                  gint expunge,
                  GError **error)
{
	CamelStoreClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->sync != NULL, FALSE);

	success = class->sync (store, expunge, error);
	CAMEL_CHECK_GERROR (store, sync, success, error);

	return success;
}

static void
add_special_info (CamelStore *store,
                  CamelFolderInfo *info,
                  const gchar *name,
                  const gchar *translated,
                  gboolean unread_count,
                  guint32 flags)
{
	CamelFolderInfo *fi, *vinfo, *parent;
	gchar *uri, *path;
	CamelURL *url;

	g_return_if_fail (info != NULL);

	parent = NULL;
	for (fi = info; fi; fi = fi->next) {
		if (!strcmp (fi->full_name, name))
			break;
		parent = fi;
	}

	/* create our vTrash/vJunk URL */
	url = camel_url_new (info->uri, NULL);
	if (((CamelService *) store)->provider->url_flags & CAMEL_URL_FRAGMENT_IS_PATH) {
		camel_url_set_fragment (url, name);
	} else {
		path = g_strdup_printf ("/%s", name);
		camel_url_set_path (url, path);
		g_free (path);
	}

	uri = camel_url_to_string (url, CAMEL_URL_HIDE_ALL);
	camel_url_free (url);

	if (fi) {
		/* We're going to replace the physical Trash/Junk folder with our vTrash/vJunk folder */
		vinfo = fi;
		g_free (vinfo->full_name);
		g_free (vinfo->name);
		g_free (vinfo->uri);
	} else {
		/* There wasn't a Trash/Junk folder so create a new folder entry */
		vinfo = camel_folder_info_new ();

		g_assert(parent != NULL);

		vinfo->flags |= CAMEL_FOLDER_NOINFERIORS | CAMEL_FOLDER_SUBSCRIBED;

		/* link it into the right spot */
		vinfo->next = parent->next;
		parent->next = vinfo;
	}

	/* Fill in the new fields */
	vinfo->flags |= flags;
	vinfo->full_name = g_strdup (name);
	vinfo->name = g_strdup (translated);
	vinfo->uri = uri;
	if (!unread_count)
		vinfo->unread = -1;
}

static void
dump_fi (CamelFolderInfo *fi, gint depth)
{
	gchar *s;

	s = g_alloca(depth+1);
	memset(s, ' ', depth);
	s[depth] = 0;

	while (fi) {
		printf("%suri: %s\n", s, fi->uri);
		printf("%sfull_name: %s\n", s, fi->full_name);
		printf("%sflags: %08x\n", s, fi->flags);
		dump_fi(fi->child, depth+2);
		fi = fi->next;
	}
}

/**
 * camel_store_get_folder_info:
 * @store: a #CamelStore object
 * @top: the name of the folder to start from
 * @flags: various CAMEL_STORE_FOLDER_INFO_* flags to control behavior
 * @error: return location for a #GError, or %NULL
 *
 * This fetches information about the folder structure of @store,
 * starting with @top, and returns a tree of CamelFolderInfo
 * structures. If @flags includes #CAMEL_STORE_FOLDER_INFO_SUBSCRIBED,
 * only subscribed folders will be listed.   If the store doesn't support
 * subscriptions, then it will list all folders.  If @flags includes
 * #CAMEL_STORE_FOLDER_INFO_RECURSIVE, the returned tree will include
 * all levels of hierarchy below @top. If not, it will only include
 * the immediate subfolders of @top. If @flags includes
 * #CAMEL_STORE_FOLDER_INFO_FAST, the unread_message_count fields of
 * some or all of the structures may be set to %-1, if the store cannot
 * determine that information quickly.  If @flags includes
 * #CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL, don't include special virtual
 * folders (such as vTrash or vJunk).
 *
 * The CAMEL_STORE_FOLDER_INFO_FAST flag should be considered
 * deprecated; most backends will behave the same whether it is
 * supplied or not.  The only guaranteed way to get updated folder
 * counts is to both open the folder and invoke refresh_info() it.
 *
 * Returns: a #CamelFolderInfo tree, which must be freed with
 * #camel_store_free_folder_info, or %NULL.
 **/
CamelFolderInfo *
camel_store_get_folder_info (CamelStore *store,
                             const gchar *top,
                             guint32 flags,
                             GError **error)
{
	CamelStoreClass *class;
	CamelFolderInfo *info;

	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->get_folder_info != NULL, NULL);

	info = class->get_folder_info (store, top, flags, error);
	if (!(flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED))
		CAMEL_CHECK_GERROR (store, get_folder_info, info != NULL, error);

	if (info && (top == NULL || *top == '\0') && (flags & CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL) == 0) {
		if (info->uri && (store->flags & CAMEL_STORE_VTRASH))
			/* the name of the Trash folder, used for deleted messages */
			add_special_info (store, info, CAMEL_VTRASH_NAME, _("Trash"), FALSE, CAMEL_FOLDER_VIRTUAL|CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_VTRASH|CAMEL_FOLDER_TYPE_TRASH);
		if (info->uri && (store->flags & CAMEL_STORE_VJUNK))
			/* the name of the Junk folder, used for spam messages */
			add_special_info (store, info, CAMEL_VJUNK_NAME, _("Junk"), TRUE, CAMEL_FOLDER_VIRTUAL|CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_VTRASH|CAMEL_FOLDER_TYPE_JUNK);
	} else if (!info && top && (flags & CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL) == 0) {
		CamelFolderInfo *root_info = NULL;

		if ((store->flags & CAMEL_STORE_VTRASH) != 0 && g_str_equal (top, CAMEL_VTRASH_NAME)) {
			root_info = class->get_folder_info (store, NULL, flags & (~CAMEL_STORE_FOLDER_INFO_RECURSIVE), error);
			if (root_info && root_info->uri)
				add_special_info (store, root_info, CAMEL_VTRASH_NAME, _("Trash"), FALSE, CAMEL_FOLDER_VIRTUAL|CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_VTRASH|CAMEL_FOLDER_TYPE_TRASH);
		} else if ((store->flags & CAMEL_STORE_VJUNK) != 0 && g_str_equal (top, CAMEL_VJUNK_NAME)) {
			root_info = class->get_folder_info (store, NULL, flags & (~CAMEL_STORE_FOLDER_INFO_RECURSIVE), error);
			if (root_info && root_info->uri)
				add_special_info (store, root_info, CAMEL_VJUNK_NAME, _("Junk"), TRUE, CAMEL_FOLDER_VIRTUAL|CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_VTRASH|CAMEL_FOLDER_TYPE_JUNK);
		}

		if (root_info) {
			info = root_info->next;
			root_info->next = NULL;
			info->next = NULL;
			info->parent = NULL;

			camel_store_free_folder_info (store, root_info);
		}
	}

	if (camel_debug_start("store:folder_info")) {
		gchar *url = camel_url_to_string(((CamelService *)store)->url, CAMEL_URL_HIDE_ALL);
		printf("Get folder info(%p:%s, '%s') =\n", (gpointer) store, url, top?top:"<null>");
		g_free(url);
		dump_fi(info, 2);
		camel_debug_end();
	}

	return info;
}

/**
 * camel_store_free_folder_info:
 * @store: a #CamelStore object
 * @fi: a #CamelFolderInfo as gotten via #camel_store_get_folder_info
 *
 * Frees the data returned by #camel_store_get_folder_info. If @fi is %NULL,
 * nothing is done, the routine simply returns.
 **/
void
camel_store_free_folder_info (CamelStore *store,
                              CamelFolderInfo *fi)
{
	CamelStoreClass *class;

	g_return_if_fail (CAMEL_IS_STORE (store));

	if (fi == NULL)
		return;

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_if_fail (class->free_folder_info != NULL);

	class->free_folder_info (store, fi);
}

/**
 * camel_store_free_folder_info_full:
 * @store: a #CamelStore object
 * @fi: a #CamelFolderInfo as gotten via #camel_store_get_folder_info
 *
 * An implementation for #CamelStore::free_folder_info. Frees all
 * of the data.
 **/
void
camel_store_free_folder_info_full (CamelStore *store,
                                   CamelFolderInfo *fi)
{
	camel_folder_info_free (fi);
}

/**
 * camel_store_free_folder_info_nop:
 * @store: a #CamelStore object
 * @fi: a #CamelFolderInfo as gotten via #camel_store_get_folder_info
 *
 * An implementation for #CamelStore::free_folder_info. Does nothing.
 **/
void
camel_store_free_folder_info_nop (CamelStore *store,
                                  CamelFolderInfo *fi)
{
	;
}

/**
 * camel_folder_info_free:
 * @fi: a #CamelFolderInfo
 *
 * Frees @fi.
 **/
void
camel_folder_info_free (CamelFolderInfo *fi)
{
	if (fi != NULL) {
		camel_folder_info_free (fi->next);
		camel_folder_info_free (fi->child);
		g_free (fi->name);
		g_free (fi->full_name);
		g_free (fi->uri);
		g_slice_free (CamelFolderInfo, fi);
	}
}

/**
 * camel_folder_info_new:
 *
 * Returns: a new empty CamelFolderInfo instance
 *
 * Since: 2.22
 **/
CamelFolderInfo *
camel_folder_info_new (void)
{
	return g_slice_new0 (CamelFolderInfo);
}

static gint
folder_info_cmp (gconstpointer ap,
                 gconstpointer bp)
{
	const CamelFolderInfo *a = ((CamelFolderInfo **)ap)[0];
	const CamelFolderInfo *b = ((CamelFolderInfo **)bp)[0];

	return strcmp (a->full_name, b->full_name);
}

/**
 * camel_folder_info_build:
 * @folders: an array of #CamelFolderInfo
 * @namespace: an ignorable prefix on the folder names
 * @separator: the hieararchy separator character
 * @short_names: %TRUE if the (short) name of a folder is the part after
 * the last @separator in the full name. %FALSE if it is the full name.
 *
 * This takes an array of folders and attaches them together according
 * to the hierarchy described by their full_names and @separator. If
 * @namespace is non-%NULL, then it will be ignored as a full_name
 * prefix, for purposes of comparison. If necessary,
 * #camel_folder_info_build will create additional #CamelFolderInfo with
 * %NULL urls to fill in gaps in the tree. The value of @short_names
 * is used in constructing the names of these intermediate folders.
 *
 * NOTE: This is deprected, do not use this.
 * FIXME: remove this/move it to imap, which is the only user of it now.
 *
 * Returns: the top level of the tree of linked folder info.
 **/
CamelFolderInfo *
camel_folder_info_build (GPtrArray *folders,
                         const gchar *namespace,
                         gchar separator,
                         gboolean short_names)
{
	CamelFolderInfo *fi, *pfi, *top = NULL, *tail = NULL;
	GHashTable *hash;
	gchar *p, *pname;
	gint i, nlen;

	if (!namespace)
		namespace = "";
	nlen = strlen (namespace);

	qsort (folders->pdata, folders->len, sizeof (folders->pdata[0]), folder_info_cmp);

	/* Hash the folders. */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (i = 0; i < folders->len; i++) {
		fi = folders->pdata[i];
		g_hash_table_insert (hash, g_strdup (fi->full_name), fi);
	}

	/* Now find parents. */
	for (i = 0; i < folders->len; i++) {
		fi = folders->pdata[i];
		if (!strncmp (namespace, fi->full_name, nlen)
		    && (p = strrchr(fi->full_name+nlen, separator))) {
			pname = g_strndup(fi->full_name, p - fi->full_name);
			pfi = g_hash_table_lookup(hash, pname);
			if (pfi) {
				g_free (pname);
			} else {
				/* we are missing a folder in the heirarchy so
				   create a fake folder node */
				const gchar *path;
				CamelURL *url;
				gchar *sep;

				pfi = camel_folder_info_new ();
				if (short_names) {
					pfi->name = strrchr (pname, separator);
					if (pfi->name)
						pfi->name = g_strdup (pfi->name + 1);
					else
						pfi->name = g_strdup (pname);
				} else
					pfi->name = g_strdup (pname);

				url = camel_url_new (fi->uri, NULL);
				if (url->fragment)
					path = url->fragment;
				else
					path = url->path + 1;

				sep = strrchr (path, separator);
				if (sep)
					*sep = '\0';
				else {
					d(g_warning ("huh, no \"%c\" in \"%s\"?", separator, fi->uri));
				}

				pfi->full_name = g_strdup (path);

				/* since this is a "fake" folder node, it is not selectable */
				camel_url_set_param (url, "noselect", "yes");
				pfi->uri = camel_url_to_string (url, 0);
				camel_url_free (url);

				g_hash_table_insert (hash, pname, pfi);
				g_ptr_array_add (folders, pfi);
			}
			tail = (CamelFolderInfo *)&pfi->child;
			while (tail->next)
				tail = tail->next;
			tail->next = fi;
			fi->parent = pfi;
		} else if (!top)
			top = fi;
	}
	g_hash_table_destroy (hash);

	/* Link together the top-level folders */
	tail = top;
	for (i = 0; i < folders->len; i++) {
		fi = folders->pdata[i];

		if (fi->child)
			fi->flags &= ~CAMEL_FOLDER_NOCHILDREN;

		if (fi->parent || fi == top)
			continue;
		if (tail == NULL) {
			tail = fi;
			top = fi;
		} else {
			tail->next = fi;
			tail = fi;
		}
	}

	return top;
}

static CamelFolderInfo *
folder_info_clone_rec (CamelFolderInfo *fi,
                       CamelFolderInfo *parent)
{
	CamelFolderInfo *info;

	info = camel_folder_info_new ();
	info->parent = parent;
	info->uri = g_strdup(fi->uri);
	info->name = g_strdup(fi->name);
	info->full_name = g_strdup(fi->full_name);
	info->unread = fi->unread;
	info->flags = fi->flags;

	if (fi->next)
		info->next = folder_info_clone_rec(fi->next, parent);
	else
		info->next = NULL;

	if (fi->child)
		info->child = folder_info_clone_rec(fi->child, info);
	else
		info->child = NULL;

	return info;
}

/**
 * camel_folder_info_clone:
 * @fi: a #CamelFolderInfo
 *
 * Clones @fi recursively.
 *
 * Returns: the cloned #CamelFolderInfo tree.
 **/
CamelFolderInfo *
camel_folder_info_clone (CamelFolderInfo *fi)
{
	if (fi == NULL)
		return NULL;

	return folder_info_clone_rec(fi, NULL);
}

/**
 * camel_store_supports_subscriptions:
 * @store: a #CamelStore object
 *
 * Get whether or not @store supports subscriptions to folders.
 *
 * Returns: %TRUE if folder subscriptions are supported or %FALSE otherwise
 **/
gboolean
camel_store_supports_subscriptions (CamelStore *store)
{
	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);

	return (store->flags & CAMEL_STORE_SUBSCRIPTIONS);
}

/**
 * camel_store_folder_is_subscribed:
 * @store: a #CamelStore object
 * @folder_name: full path of the folder
 *
 * Find out if a folder has been subscribed to.
 *
 * Returns: %TRUE if the folder has been subscribed to or %FALSE otherwise
 **/
gboolean
camel_store_folder_is_subscribed (CamelStore *store,
                                  const gchar *folder_name)
{
	CamelStoreClass *class;
	gboolean is_subscribed;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (store->flags & CAMEL_STORE_SUBSCRIPTIONS, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->folder_is_subscribed != NULL, FALSE);

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	is_subscribed = class->folder_is_subscribed (store, folder_name);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return is_subscribed;
}

/**
 * camel_store_subscribe_folder:
 * @store: a #CamelStore object
 * @folder_name: full path of the folder
 * @error: return location for a #GError, or %NULL
 *
 * Subscribe to the folder described by @folder_name.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_store_subscribe_folder (CamelStore *store,
                              const gchar *folder_name,
                              GError **error)
{
	CamelStoreClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (store->flags & CAMEL_STORE_SUBSCRIPTIONS, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->subscribe_folder != NULL, FALSE);

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	success = class->subscribe_folder (store, folder_name, error);
	CAMEL_CHECK_GERROR (store, subscribe_folder, success, error);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return success;
}

/**
 * camel_store_unsubscribe_folder:
 * @store: a #CamelStore object
 * @folder_name: full path of the folder
 * @error: return location for a #GError, or %NULL
 *
 * Unsubscribe from the folder described by @folder_name.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_store_unsubscribe_folder (CamelStore *store,
                                const gchar *folder_name,
                                GError **error)
{
	CamelStoreClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (folder_name != NULL, FALSE);
	g_return_val_if_fail (store->flags & CAMEL_STORE_SUBSCRIPTIONS, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->unsubscribe_folder != NULL, FALSE);

	camel_store_lock (store, CAMEL_STORE_FOLDER_LOCK);

	success = class->unsubscribe_folder (store, folder_name, error);
	CAMEL_CHECK_GERROR (store, unsubscribe_folder, success, error);

	if (success)
		cs_delete_cached_folder (store, folder_name);

	camel_store_unlock (store, CAMEL_STORE_FOLDER_LOCK);

	return success;
}

/**
 * camel_store_noop:
 * @store: a #CamelStore object
 * @error: return location for a #GError, or %NULL
 *
 * Pings @store so that its connection doesn't timeout.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_store_noop (CamelStore *store,
                  GError **error)
{
	CamelStoreClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->noop != NULL, FALSE);

	success = class->noop (store, error);
	CAMEL_CHECK_GERROR (store, noop, success, error);

	return success;
}

/**
 * camel_store_folder_uri_equal:
 * @store: a #CamelStore object
 * @uri0: a folder uri
 * @uri1: another folder uri
 *
 * Compares two folder uris to check that they are equal.
 *
 * Returns: %TRUE if they are equal or %FALSE otherwise
 **/
gint
camel_store_folder_uri_equal (CamelStore *store,
                              const gchar *uri0,
                              const gchar *uri1)
{
	CamelStoreClass *class;
	CamelProvider *provider;
	CamelURL *url0, *url1;
	gint equal;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (uri0 && uri1, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->compare_folder_name != NULL, FALSE);

	provider = ((CamelService *) store)->provider;

	if (!(url0 = camel_url_new (uri0, NULL)))
		return FALSE;

	if (!(url1 = camel_url_new (uri1, NULL))) {
		camel_url_free (url0);
		return FALSE;
	}

	if ((equal = provider->url_equal (url0, url1))) {
		const gchar *name0, *name1;

		if (provider->url_flags & CAMEL_URL_FRAGMENT_IS_PATH) {
			name0 = url0->fragment;
			name1 = url1->fragment;
		} else {
			name0 = url0->path && url0->path[0] == '/' ? url0->path + 1 : url0->path;
			name1 = url1->path && url1->path[0] == '/' ? url1->path + 1 : url1->path;
		}

		if (name0 == NULL)
			g_warning("URI is badly formed, missing folder name: %s", uri0);

		if (name1 == NULL)
			g_warning("URI is badly formed, missing folder name: %s", uri1);

		equal = name0 && name1 && class->compare_folder_name (name0, name1);
	}

	camel_url_free (url0);
	camel_url_free (url1);

	return equal;
}

/**
 * camel_store_can_refresh_folder
 * @store: a #CamelStore
 * @info: a #CamelFolderInfo
 * @error: return location for a #GError, or %NULL
 *
 * Returns if this folder (param info) should be checked for new mail or not.
 * It should not look into sub infos (info->child) or next infos, it should
 * return value only for the actual folder info.
 * Default behavior is that all Inbox folders are intended to be refreshed.
 *
 * Returns: whether folder should be checked for new mails
 *
 * Since: 2.22
 **/
gboolean
camel_store_can_refresh_folder (CamelStore *store,
                                CamelFolderInfo *info,
                                GError **error)
{
	CamelStoreClass *class;

	g_return_val_if_fail (CAMEL_IS_STORE (store), FALSE);
	g_return_val_if_fail (info != NULL, FALSE);

	class = CAMEL_STORE_GET_CLASS (store);
	g_return_val_if_fail (class->can_refresh_folder != NULL, FALSE);

	return class->can_refresh_folder (store, info, error);
}

/**
 * camel_store_lock:
 * @store: a #CamelStore
 * @lock: lock type to lock
 *
 * Locks #store's #lock. Unlock it with camel_store_unlock().
 *
 * Since: 2.32
 **/
void
camel_store_lock (CamelStore *store,
                  CamelStoreLock lock)
{
	g_return_if_fail (CAMEL_IS_STORE (store));

	switch (lock) {
		case CAMEL_STORE_FOLDER_LOCK:
			g_static_rec_mutex_lock (&store->priv->folder_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_store_unlock:
 * @store: a #CamelStore
 * @lock: lock type to unlock
 *
 * Unlocks #store's #lock, previously locked with camel_store_lock().
 *
 * Since: 2.32
 **/
void
camel_store_unlock (CamelStore *store,
                    CamelStoreLock lock)
{
	g_return_if_fail (CAMEL_IS_STORE (store));

	switch (lock) {
		case CAMEL_STORE_FOLDER_LOCK:
			g_static_rec_mutex_unlock (&store->priv->folder_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

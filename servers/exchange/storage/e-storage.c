/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-storage.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Ettore Perazzoli
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-storage.h"

#include "e-folder-tree.h"

#include <glib/gi18n-lib.h>
#include <libedataserver/e-data-server-util.h>

#include <string.h>

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (EStorage, e_storage, G_TYPE_OBJECT)

struct EStoragePrivate {
	/* The set of folders we have in this storage.  */
	EFolderTree *folder_tree;

	/* Internal name of the storage */
	gchar *name;
};

enum {
	NEW_FOLDER,
	UPDATED_FOLDER,
	REMOVED_FOLDER,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* Destroy notification function for the folders in the tree.  */

static void
folder_destroy_notify (EFolderTree *tree,
		       const gchar *path,
		       gpointer data,
		       gpointer closure)
{
	EFolder *e_folder;

	if (data == NULL) {
		/* The root folder has no EFolder associated to it.  */
		return;
	}

	e_folder = E_FOLDER (data);
	g_object_unref (e_folder);
}

/* Signal callbacks for the EFolders.  */

static void
folder_changed_cb (EFolder *folder,
		   gpointer data)
{
	EStorage *storage;
	EStoragePrivate *priv;
	const gchar *path, *p;
	gboolean highlight;

	g_assert (E_IS_STORAGE (data));

	storage = E_STORAGE (data);
	priv = storage->priv;

	path = e_folder_tree_get_path_for_data (priv->folder_tree, folder);
	g_assert (path != NULL);

	g_signal_emit (storage, signals[UPDATED_FOLDER], 0, path);

	highlight = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (folder), "last_highlight"));
	if (highlight != e_folder_get_highlighted (folder)) {
		highlight = !highlight;
		g_object_set_data (G_OBJECT (folder), "last_highlight", GINT_TO_POINTER (highlight));
		p = strrchr (path, '/');
		if (p && p != path) {
			gchar *name;

			name = g_strndup (path, p - path);
			folder = e_folder_tree_get_folder (priv->folder_tree, name);
			g_free (name);
			if (folder)
				e_folder_set_child_highlight (folder, highlight);
		}
	}
}

/* GObject methods.  */

static void
impl_finalize (GObject *object)
{
	EStorage *storage;
	EStoragePrivate *priv;

	storage = E_STORAGE (object);
	priv = storage->priv;

	if (priv->folder_tree != NULL)
		e_folder_tree_destroy (priv->folder_tree);

	g_free (priv->name);

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* EStorage methods.  */

static GList *
impl_get_subfolder_paths (EStorage *storage,
			  const gchar *path)
{
	EStoragePrivate *priv;

	priv = storage->priv;

	return e_folder_tree_get_subfolders (priv->folder_tree, path);
}

static EFolder *
impl_get_folder (EStorage *storage,
		 const gchar *path)
{
	EStoragePrivate *priv;
	EFolder *e_folder;

	priv = storage->priv;

	e_folder = (EFolder *) e_folder_tree_get_folder (priv->folder_tree, path);

	return e_folder;
}

static const gchar *
impl_get_name (EStorage *storage)
{
	return storage->priv->name;
}

static void
impl_async_create_folder (EStorage *storage,
			  const gchar *path,
			  const gchar *type,
			  EStorageResultCallback callback,
			  gpointer data)
{
	(* callback) (storage, E_STORAGE_NOTIMPLEMENTED, data);
}

static void
impl_async_remove_folder (EStorage *storage,
			  const gchar *path,
			  EStorageResultCallback callback,
			  gpointer data)
{
	(* callback) (storage, E_STORAGE_NOTIMPLEMENTED, data);
}

static void
impl_async_xfer_folder (EStorage *storage,
			const gchar *source_path,
			const gchar *destination_path,
			gboolean remove_source,
			EStorageResultCallback callback,
			gpointer data)
{
	(* callback) (storage, E_STORAGE_NOTIMPLEMENTED, data);
}

static void
impl_async_open_folder (EStorage *storage,
			const gchar *path,
			EStorageDiscoveryCallback callback,
			gpointer data)
{
	(* callback) (storage, E_STORAGE_NOTIMPLEMENTED, NULL, data);
}

static gboolean
impl_will_accept_folder (EStorage *storage,
			 EFolder *new_parent,
			 EFolder *source)
{
	EStoragePrivate *priv = storage->priv;
	const gchar *parent_path, *source_path;
	gint source_len;

	/* By default, we only disallow dragging a folder into
	 * a subfolder of itself.
	 */

	if (new_parent == source)
		return FALSE;

	parent_path = e_folder_tree_get_path_for_data (priv->folder_tree,
						       new_parent);
	source_path = e_folder_tree_get_path_for_data (priv->folder_tree,
						       source);
	if (!parent_path || !source_path)
		return FALSE;

	source_len = strlen (source_path);
	if (!strncmp (parent_path, source_path, source_len) &&
	    parent_path[source_len] == '/')
		return FALSE;

	return TRUE;
}

static void
impl_async_discover_shared_folder (EStorage *storage,
				   const gchar *owner,
				   const gchar *folder_name,
				   EStorageDiscoveryCallback callback,
				   gpointer data)
{
	(* callback) (storage, E_STORAGE_NOTIMPLEMENTED, NULL, data);
}

static void
impl_async_remove_shared_folder (EStorage *storage,
				 const gchar *path,
				 EStorageResultCallback callback,
				 gpointer data)
{
	(* callback) (storage, E_STORAGE_NOTIMPLEMENTED, data);
}

/* Initialization.  */

static void
e_storage_class_init (EStorageClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	parent_class = g_type_class_ref (PARENT_TYPE);

	object_class->finalize = impl_finalize;

	class->get_subfolder_paths = impl_get_subfolder_paths;
	class->get_folder          = impl_get_folder;
	class->get_name            = impl_get_name;
	class->async_create_folder = impl_async_create_folder;
	class->async_remove_folder = impl_async_remove_folder;
	class->async_xfer_folder   = impl_async_xfer_folder;
	class->async_open_folder   = impl_async_open_folder;
	class->will_accept_folder  = impl_will_accept_folder;

	class->async_discover_shared_folder  = impl_async_discover_shared_folder;
	class->async_remove_shared_folder    = impl_async_remove_shared_folder;
	signals[NEW_FOLDER] =
		g_signal_new ("new_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EStorageClass, new_folder),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);
	signals[UPDATED_FOLDER] =
		g_signal_new ("updated_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EStorageClass, updated_folder),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);
	signals[REMOVED_FOLDER] =
		g_signal_new ("removed_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (EStorageClass, removed_folder),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);
}

static void
e_storage_init (EStorage *storage)
{
	EStoragePrivate *priv;

	priv = g_new0 (EStoragePrivate, 1);

	priv->folder_tree   = e_folder_tree_new (folder_destroy_notify, NULL);

	storage->priv = priv;
}

/* Creation.  */

void
e_storage_construct (EStorage *storage,
		     const gchar *name,
		     EFolder *root_folder)
{
	EStoragePrivate *priv;

	g_return_if_fail (E_IS_STORAGE (storage));

	priv = storage->priv;

	priv->name = g_strdup (name);

	e_storage_new_folder (storage, "/", root_folder);
}

EStorage *
e_storage_new (const gchar *name,
	       EFolder *root_folder)
{
	EStorage *new;

	new = g_object_new (e_storage_get_type (), NULL);

	e_storage_construct (new, name, root_folder);

	return new;
}

gboolean
e_storage_path_is_absolute (const gchar *path)
{
	g_return_val_if_fail (path != NULL, FALSE);

	return *path == '/';
}

gboolean
e_storage_path_is_relative (const gchar *path)
{
	g_return_val_if_fail (path != NULL, FALSE);

	return *path != '/';
}

GList *
e_storage_get_subfolder_paths (EStorage *storage,
			       const gchar *path)
{
	g_return_val_if_fail (E_IS_STORAGE (storage), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (g_path_is_absolute (path), NULL);

	return (* E_STORAGE_GET_CLASS (storage)->get_subfolder_paths) (storage, path);
}

EFolder *
e_storage_get_folder (EStorage *storage,
		      const gchar *path)
{
	g_return_val_if_fail (E_IS_STORAGE (storage), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (e_storage_path_is_absolute (path), NULL);

	return (* E_STORAGE_GET_CLASS (storage)->get_folder) (storage, path);
}

const gchar *
e_storage_get_name (EStorage *storage)
{
	g_return_val_if_fail (E_IS_STORAGE (storage), NULL);

	return (* E_STORAGE_GET_CLASS (storage)->get_name) (storage);
}

/* Folder operations.  */

void
e_storage_async_create_folder (EStorage *storage,
			       const gchar *path,
			       const gchar *type,
			       EStorageResultCallback callback,
			       gpointer data)
{
	g_return_if_fail (E_IS_STORAGE (storage));
	g_return_if_fail (path != NULL);
	g_return_if_fail (g_path_is_absolute (path));
	g_return_if_fail (type != NULL);
	g_return_if_fail (callback != NULL);

	(* E_STORAGE_GET_CLASS (storage)->async_create_folder) (storage, path, type, callback, data);
}

void
e_storage_async_remove_folder (EStorage              *storage,
			       const gchar            *path,
			       EStorageResultCallback callback,
			       void                  *data)
{
	g_return_if_fail (E_IS_STORAGE (storage));
	g_return_if_fail (path != NULL);
	g_return_if_fail (g_path_is_absolute (path));
	g_return_if_fail (callback != NULL);

	(* E_STORAGE_GET_CLASS (storage)->async_remove_folder) (storage, path, callback, data);
}

void
e_storage_async_xfer_folder (EStorage *storage,
			     const gchar *source_path,
			     const gchar *destination_path,
			     const gboolean remove_source,
			     EStorageResultCallback callback,
			     gpointer data)
{
	g_return_if_fail (E_IS_STORAGE (storage));
	g_return_if_fail (source_path != NULL);
	g_return_if_fail (g_path_is_absolute (source_path));
	g_return_if_fail (destination_path != NULL);
	g_return_if_fail (g_path_is_absolute (destination_path));

	if (strcmp (source_path, destination_path) == 0) {
		(* callback) (storage, E_STORAGE_OK, data);
		return;
	}

	if (remove_source) {
		gint destination_len;
		gint source_len;

		source_len = strlen (source_path);
		destination_len = strlen (destination_path);

		if (source_len < destination_len
		    && destination_path[source_len] == '/'
		    && strncmp (destination_path, source_path, source_len) == 0) {
			(* callback) (storage, E_STORAGE_CANTMOVETODESCENDANT, data);
			return;
		}
	}

	(* E_STORAGE_GET_CLASS (storage)->async_xfer_folder) (storage, source_path, destination_path, remove_source, callback, data);
}

void
e_storage_async_open_folder (EStorage *storage,
			     const gchar *path,
			     EStorageDiscoveryCallback callback,
			     gpointer data)
{
	EStoragePrivate *priv;
	EFolder *folder;

	g_return_if_fail (E_IS_STORAGE (storage));
	g_return_if_fail (path != NULL);
	g_return_if_fail (g_path_is_absolute (path));

	priv = storage->priv;

	folder = e_folder_tree_get_folder (priv->folder_tree, path);
	if (folder == NULL) {
		(* callback) (storage, E_STORAGE_NOTFOUND, path, data);
		return;
	}

	if (! e_folder_get_has_subfolders (folder)) {
		(* callback) (storage, E_STORAGE_OK, path, data);
		return;
	}

	(* E_STORAGE_GET_CLASS (storage)->async_open_folder) (storage, path, callback, data);
}

gboolean
e_storage_will_accept_folder (EStorage *storage,
			      EFolder *new_parent, EFolder *source)
{
	g_return_val_if_fail (E_IS_STORAGE (storage), FALSE);
	g_return_val_if_fail (E_IS_FOLDER (new_parent), FALSE);
	g_return_val_if_fail (E_IS_FOLDER (source), FALSE);

	return (* E_STORAGE_GET_CLASS (storage)->will_accept_folder) (storage, new_parent, source);
}

/* Shared folders.  */

void
e_storage_async_discover_shared_folder (EStorage *storage,
					const gchar *owner,
					const gchar *folder_name,
					EStorageDiscoveryCallback callback,
					gpointer data)
{
	g_return_if_fail (E_IS_STORAGE (storage));
	g_return_if_fail (owner != NULL);
	g_return_if_fail (folder_name != NULL);

	(* E_STORAGE_GET_CLASS (storage)->async_discover_shared_folder) (storage, owner, folder_name, callback, data);
}

void
e_storage_cancel_discover_shared_folder (EStorage *storage,
					 const gchar *owner,
					 const gchar *folder_name)
{
	g_return_if_fail (E_IS_STORAGE (storage));
	g_return_if_fail (owner != NULL);
	g_return_if_fail (folder_name != NULL);
	g_return_if_fail (E_STORAGE_GET_CLASS (storage)->cancel_discover_shared_folder != NULL);

	(* E_STORAGE_GET_CLASS (storage)->cancel_discover_shared_folder) (storage, owner, folder_name);
}

void
e_storage_async_remove_shared_folder (EStorage *storage,
				      const gchar *path,
				      EStorageResultCallback callback,
				      gpointer data)
{
	g_return_if_fail (E_IS_STORAGE (storage));
	g_return_if_fail (path != NULL);
	g_return_if_fail (g_path_is_absolute (path));

	(* E_STORAGE_GET_CLASS (storage)->async_remove_shared_folder) (storage, path, callback, data);
}

const gchar *
e_storage_result_to_string (EStorageResult result)
{
	switch (result) {
	case E_STORAGE_OK:
		return _("No error");
	case E_STORAGE_GENERICERROR:
		return _("Generic error");
	case E_STORAGE_EXISTS:
		return _("A folder with the same name already exists");
	case E_STORAGE_INVALIDTYPE:
		return _("The specified folder type is not valid");
	case E_STORAGE_IOERROR:
		return _("I/O error");
	case E_STORAGE_NOSPACE:
		return _("Not enough space to create the folder");
	case E_STORAGE_NOTEMPTY:
		return _("The folder is not empty");
	case E_STORAGE_NOTFOUND:
		return _("The specified folder was not found");
	case E_STORAGE_NOTIMPLEMENTED:
		return _("Function not implemented in this storage");
	case E_STORAGE_PERMISSIONDENIED:
		return _("Permission denied");
	case E_STORAGE_UNSUPPORTEDOPERATION:
		return _("Operation not supported");
	case E_STORAGE_UNSUPPORTEDTYPE:
		return _("The specified type is not supported in this storage");
	case E_STORAGE_CANTCHANGESTOCKFOLDER:
		return _("The specified folder cannot be modified or removed");
	case E_STORAGE_CANTMOVETODESCENDANT:
		return _("Cannot make a folder a child of one of its descendants");
	case E_STORAGE_INVALIDNAME:
		return _("Cannot create a folder with that name");
	case E_STORAGE_NOTONLINE:
		return _("This operation cannot be performed in off-line mode");
	default:
		return _("Unknown error");
	}
}

/* Public utility functions.  */

typedef struct {
	const gchar *physical_uri;
	gchar *retval;
} GetPathForPhysicalUriForeachData;

static void
get_path_for_physical_uri_foreach (EFolderTree *folder_tree,
				   const gchar *path,
				   gpointer path_data,
				   gpointer user_data)
{
	GetPathForPhysicalUriForeachData *foreach_data;
	const gchar *physical_uri;
	EFolder *e_folder;

	foreach_data = (GetPathForPhysicalUriForeachData *) user_data;
	if (foreach_data->retval != NULL)
		return;

	e_folder = (EFolder *) path_data;
	if (e_folder == NULL)
		return;

	physical_uri = e_folder_get_physical_uri (e_folder);
	if (physical_uri == NULL)
		return;

	if (strcmp (foreach_data->physical_uri, physical_uri) == 0)
		foreach_data->retval = g_strdup (path);
}

/**
 * e_storage_get_path_for_physical_uri:
 * @storage: A storage
 * @physical_uri: A physical URI
 *
 * Look for the folder having the specified @physical_uri.
 *
 * Return value: The path of the folder having the specified @physical_uri in
 * @storage.  If such a folder does not exist, just return NULL.  The return
 * value must be freed by the caller.
 **/
gchar *
e_storage_get_path_for_physical_uri (EStorage *storage,
				     const gchar *physical_uri)
{
	GetPathForPhysicalUriForeachData foreach_data;
	EStoragePrivate *priv;

	g_return_val_if_fail (E_IS_STORAGE (storage), NULL);
	g_return_val_if_fail (physical_uri != NULL, NULL);

	priv = storage->priv;

	foreach_data.physical_uri = physical_uri;
	foreach_data.retval       = NULL;

	e_folder_tree_foreach (priv->folder_tree, get_path_for_physical_uri_foreach, &foreach_data);

	return foreach_data.retval;
}

/* Protected functions.  */

/* These functions are used by subclasses to add and remove folders from the
   state stored in the storage object.  */

static void
remove_subfolders_except (EStorage *storage, const gchar *path, const gchar *except)
{
	EStoragePrivate *priv;
	GList *subfolders, *f;
	const gchar *folder_path;

	priv = storage->priv;

	subfolders = e_folder_tree_get_subfolders (priv->folder_tree, path);
	for (f = subfolders; f; f = f->next) {
		folder_path = f->data;
		if (!except || strcmp (folder_path, except) != 0)
			e_storage_removed_folder (storage, folder_path);
	}
	for (f = subfolders; f != NULL; f = f->next)
		g_free (f->data);

	g_list_free (subfolders);
}

gboolean
e_storage_new_folder (EStorage *storage,
		      const gchar *path,
		      EFolder *e_folder)
{
	EStoragePrivate *priv;
	gchar *parent_path, *p;
	EFolder *parent;

	g_return_val_if_fail (E_IS_STORAGE (storage), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (g_path_is_absolute (path), FALSE);
	g_return_val_if_fail (E_IS_FOLDER (e_folder), FALSE);

	priv = storage->priv;

	if (! e_folder_tree_add (priv->folder_tree, path, e_folder))
		return FALSE;

	/* If this is the child of a folder that has a pseudo child,
	 * remove the pseudo child now.
	 */
	p = strrchr (path, '/');
	if (p && p != path)
		parent_path = g_strndup (path, p - path);
	else
		parent_path = g_strdup ("/");
	parent = e_folder_tree_get_folder (priv->folder_tree, parent_path);
	if (parent && e_folder_get_has_subfolders (parent)) {
		remove_subfolders_except (storage, parent_path, path);
		e_folder_set_has_subfolders (parent, FALSE);
	}
	g_free (parent_path);

	g_signal_connect_object (e_folder, "changed", G_CALLBACK (folder_changed_cb), storage, 0);

	g_signal_emit (storage, signals[NEW_FOLDER], 0, path);

	folder_changed_cb (e_folder, storage);

	return TRUE;
}

/* This really should be called e_storage_set_has_subfolders, but then
 * it would look like it was an EStorageSet function. (Fact o' the
 * day: The word "set" has more distinct meanings than any other word
 * in the English language.) Anyway, we now return you to your
 * regularly scheduled source code, already in progress.
 */
gboolean
e_storage_declare_has_subfolders (EStorage *storage,
				  const gchar *path,
				  const gchar *message)
{
	EStoragePrivate *priv;
	EFolder *parent, *pseudofolder;
	gchar *pseudofolder_path;
	gboolean ok;

	g_return_val_if_fail (E_IS_STORAGE (storage), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (g_path_is_absolute (path), FALSE);
	g_return_val_if_fail (message != NULL, FALSE);

	priv = storage->priv;

	parent = e_folder_tree_get_folder (priv->folder_tree, path);
	if (parent == NULL)
		return FALSE;
	if (e_folder_get_has_subfolders (parent))
		return TRUE;

	remove_subfolders_except (storage, path, NULL);

	pseudofolder = e_folder_new (message, "working", "");
	if (strcmp (path, "/") == 0)
		pseudofolder_path = g_strdup_printf ("/%s", message);
	else
		pseudofolder_path = g_strdup_printf ("%s/%s", path, message);
	e_folder_set_physical_uri (pseudofolder, pseudofolder_path);

	ok = e_storage_new_folder (storage, pseudofolder_path, pseudofolder);
	g_free (pseudofolder_path);
	if (!ok) {
		g_object_unref (pseudofolder);
		return FALSE;
	}

	e_folder_set_has_subfolders (parent, TRUE);
	return TRUE;
}

gboolean
e_storage_get_has_subfolders (EStorage *storage,
			      const gchar *path)
{
	EStoragePrivate *priv;
	EFolder *folder;

	g_return_val_if_fail (E_IS_STORAGE (storage), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (g_path_is_absolute (path), FALSE);

	priv = storage->priv;

	folder = e_folder_tree_get_folder (priv->folder_tree, path);

	return folder && e_folder_get_has_subfolders (folder);
}

gboolean
e_storage_removed_folder (EStorage *storage,
			  const gchar *path)
{
	EStoragePrivate *priv;
	EFolder *folder;
	const gchar *p;

	g_return_val_if_fail (E_IS_STORAGE (storage), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (g_path_is_absolute (path), FALSE);

	priv = storage->priv;

	folder = e_folder_tree_get_folder (priv->folder_tree, path);
	if (folder == NULL)
		return FALSE;

	p = strrchr (path, '/');
	if (p != NULL && p != path) {
		EFolder *parent_folder;
		gchar *parent_path;

		parent_path = g_strndup (path, p - path);
		parent_folder = e_folder_tree_get_folder (priv->folder_tree, parent_path);

		if (e_folder_get_highlighted (folder))
			e_folder_set_child_highlight (parent_folder, FALSE);

		g_free (parent_path);
	}

	g_signal_emit (storage, signals[REMOVED_FOLDER], 0, path);

	e_folder_tree_remove (priv->folder_tree, path);

	return TRUE;
}

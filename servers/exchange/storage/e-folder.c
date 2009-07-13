/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-folder.c
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

#include "e-folder.h"

#include <string.h>
#include <glib.h>
#include <libedataserver/e-data-server-util.h>

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (EFolder, e_folder, G_TYPE_OBJECT)

struct EFolderPrivate {
	gchar *name;
	gchar *type;
	gchar *description;
	gchar *physical_uri;

	gint child_highlight;
	gint unread_count;

	/* Folders have a default sorting priority of zero; when deciding the
	   sort order in the Evolution folder tree, folders with the same
	   priority value are compared by name, while folders with a higher
	   priority number always come after the folders with a lower priority
	   number.  */
	gint sorting_priority;

	guint self_highlight : 1;
	guint is_stock : 1;
	guint can_sync_offline : 1;
	guint has_subfolders : 1;

	/* Custom icon for this folder; if NULL the folder will just use the
	   icon for its type.  */
	gchar *custom_icon_name;
};

enum {
	CHANGED,
	NAME_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* EFolder methods.   */

static gboolean
accept_drop (EFolder *folder, GdkDragContext *context,
	     const gchar *target_type,
	     GtkSelectionData *selection_data)
{
	return FALSE;
}

/* GObject methods.  */

static void
impl_finalize (GObject *object)
{
	EFolder *folder;
	EFolderPrivate *priv;

	folder = E_FOLDER (object);
	priv = folder->priv;

	g_free (priv->name);
	g_free (priv->type);
	g_free (priv->description);
	g_free (priv->physical_uri);

	g_free (priv->custom_icon_name);

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
e_folder_class_init (EFolderClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_ref (PARENT_TYPE);

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = impl_finalize;

	klass->accept_drop = accept_drop;
	signals[CHANGED] = g_signal_new ("changed",
					 G_OBJECT_CLASS_TYPE (object_class),
					 G_SIGNAL_RUN_FIRST,
					 G_STRUCT_OFFSET (EFolderClass, changed),
					 NULL, NULL,
					 g_cclosure_marshal_VOID__VOID,
					 G_TYPE_NONE, 0);

	signals[NAME_CHANGED] = g_signal_new ("name_changed",
					      G_OBJECT_CLASS_TYPE (object_class),
					      G_SIGNAL_RUN_FIRST,
					      G_STRUCT_OFFSET (EFolderClass, name_changed),
					      NULL, NULL,
					      g_cclosure_marshal_VOID__VOID,
					      G_TYPE_NONE, 0);
}

static void
e_folder_init (EFolder *folder)
{
	EFolderPrivate *priv;

	priv = g_new0 (EFolderPrivate, 1);
	folder->priv = priv;
}

void
e_folder_construct (EFolder *folder,
		    const gchar *name,
		    const gchar *type,
		    const gchar *description)
{
	EFolderPrivate *priv;

	g_return_if_fail (E_IS_FOLDER (folder));
	g_return_if_fail (name != NULL);
	g_return_if_fail (type != NULL);

	priv = folder->priv;

	priv->name        = g_strdup (name);
	priv->type        = g_strdup (type);
	priv->description = g_strdup (description);
}

EFolder *
e_folder_new (const gchar *name,
	      const gchar *type,
	      const gchar *description)
{
	EFolder *folder;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (type != NULL, NULL);
	g_return_val_if_fail (description != NULL, NULL);

	folder = g_object_new (E_TYPE_FOLDER, NULL);

	e_folder_construct (folder, name, type, description);

	return folder;
}

const gchar *
e_folder_get_name (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), NULL);

	return folder->priv->name;
}

const gchar *
e_folder_get_type_string (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), NULL);

	return folder->priv->type;
}

const gchar *
e_folder_get_description (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), NULL);

	return folder->priv->description;
}

const gchar *
e_folder_get_physical_uri (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), NULL);

	return folder->priv->physical_uri;
}

gint
e_folder_get_unread_count (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), FALSE);

	return folder->priv->unread_count;
}

gboolean
e_folder_get_highlighted (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), FALSE);

	return folder->priv->child_highlight || folder->priv->unread_count;
}

gboolean
e_folder_get_is_stock (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), FALSE);

	return folder->priv->is_stock;
}

gboolean
e_folder_get_can_sync_offline (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), FALSE);

	return folder->priv->can_sync_offline;
}

gboolean
e_folder_get_has_subfolders (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), FALSE);

	return folder->priv->has_subfolders;
}

/**
 * e_folder_get_custom_icon:
 * @folder: An EFolder
 *
 * Get the name of the custom icon for @folder, or NULL if no custom icon is
 * associated with it.
 **/
const gchar *
e_folder_get_custom_icon_name (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), NULL);

	return folder->priv->custom_icon_name;
}

/**
 * e_folder_get_sorting_priority:
 * @folder: An EFolder
 *
 * Get the sorting priority for @folder.
 *
 * Return value: Sorting priority value for @folder.
 **/
gint
e_folder_get_sorting_priority (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), 0);

	return folder->priv->sorting_priority;
}

void
e_folder_set_name (EFolder *folder,
		   const gchar *name)
{
	g_return_if_fail (E_IS_FOLDER (folder));
	g_return_if_fail (name != NULL);

	if (folder->priv->name == name)
		return;

	g_free (folder->priv->name);
	folder->priv->name = g_strdup (name);

	g_signal_emit (folder, signals[NAME_CHANGED], 0);
	g_signal_emit (folder, signals[CHANGED], 0);
}

void
e_folder_set_type_string (EFolder *folder,
			  const gchar *type)
{
	g_return_if_fail (E_IS_FOLDER (folder));
	g_return_if_fail (type != NULL);

	g_free (folder->priv->type);
	folder->priv->type = g_strdup (type);

	g_signal_emit (folder, signals[CHANGED], 0);
}

void
e_folder_set_description (EFolder *folder,
			  const gchar *description)
{
	g_return_if_fail (E_IS_FOLDER (folder));
	g_return_if_fail (description != NULL);

	g_free (folder->priv->description);
	folder->priv->description = g_strdup (description);

	g_signal_emit (folder, signals[CHANGED], 0);
}

void
e_folder_set_physical_uri (EFolder *folder,
			   const gchar *physical_uri)
{
	g_return_if_fail (E_IS_FOLDER (folder));
	g_return_if_fail (physical_uri != NULL);

	if (folder->priv->physical_uri == physical_uri)
		return;

	g_free (folder->priv->physical_uri);
	folder->priv->physical_uri = g_strdup (physical_uri);

	g_signal_emit (folder, signals[CHANGED], 0);
}

void
e_folder_set_unread_count (EFolder *folder,
			   gint unread_count)
{
	g_return_if_fail (E_IS_FOLDER (folder));

	folder->priv->unread_count = unread_count;

	g_signal_emit (folder, signals[CHANGED], 0);
}

void
e_folder_set_child_highlight (EFolder *folder,
			      gboolean highlighted)
{
	g_return_if_fail (E_IS_FOLDER (folder));

	if (highlighted)
		folder->priv->child_highlight++;
	else
		folder->priv->child_highlight--;

	g_signal_emit (folder, signals[CHANGED], 0);
}

void
e_folder_set_is_stock (EFolder *folder,
		       gboolean is_stock)
{
	g_return_if_fail (E_IS_FOLDER (folder));

	folder->priv->is_stock = !! is_stock;

	g_signal_emit (folder, signals[CHANGED], 0);
}

void
e_folder_set_can_sync_offline (EFolder *folder,
			       gboolean can_sync_offline)
{
	g_return_if_fail (E_IS_FOLDER (folder));

	folder->priv->can_sync_offline = !! can_sync_offline;

	g_signal_emit (folder, signals[CHANGED], 0);
}

void
e_folder_set_has_subfolders (EFolder *folder,
			     gboolean has_subfolders)
{
	g_return_if_fail (E_IS_FOLDER (folder));

	folder->priv->has_subfolders = !! has_subfolders;

	g_signal_emit (folder, signals[CHANGED], 0);
}

/**
 * e_folder_set_custom_icon_name:
 * @folder: An EFolder
 * @icon_name: Name of the icon to be set (to be found in the standard
 * Evolution icon dir)
 *
 * Set a custom icon for @folder (thus overriding the default icon, which is
 * the one associated to the type of the folder).
 **/
void
e_folder_set_custom_icon (EFolder *folder,
			  const gchar *icon_name)
{
	g_return_if_fail (E_IS_FOLDER (folder));

	if (icon_name == folder->priv->custom_icon_name)
		return;

	if (folder->priv->custom_icon_name == NULL
	    || (icon_name != NULL && strcmp (icon_name, folder->priv->custom_icon_name) != 0)) {
		g_free (folder->priv->custom_icon_name);
		folder->priv->custom_icon_name = g_strdup (icon_name);

		g_signal_emit (folder, signals[CHANGED], 0);
	}
}

/**
 * e_folder_set_sorting_priority:
 * @folder: An EFolder
 * @sorting_priority: A sorting priority number
 *
 * Set the sorting priority for @folder.  Folders have a default sorting
 * priority of zero; when deciding the sort order in the Evolution folder tree,
 * folders with the same priority value are compared by name, while folders
 * with a higher priority number always come after the folders with a lower
 * priority number.
 **/
void
e_folder_set_sorting_priority (EFolder *folder,
			       gint sorting_priority)
{
	g_return_if_fail (E_IS_FOLDER (folder));

	if (folder->priv->sorting_priority == sorting_priority)
		return;

	folder->priv->sorting_priority = sorting_priority;

	g_signal_emit (folder, signals[CHANGED], 0);
}

gboolean
e_folder_accept_drop (EFolder *folder, GdkDragContext *context,
		      const gchar *target_type,
		      GtkSelectionData *selection_data)
{
	g_return_val_if_fail (E_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (context != NULL, FALSE);

	return E_FOLDER_GET_CLASS (folder)->accept_drop (folder, context,
							 target_type,
							 selection_data);
}

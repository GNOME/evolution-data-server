/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-folder-tree.h
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

#ifndef _E_FOLDER_TREE_H_
#define _E_FOLDER_TREE_H_

#include <glib.h>

typedef struct EFolderTree EFolderTree;

typedef void (* EFolderDestroyNotify)   (EFolderTree *tree, const gchar *path, gpointer data, gpointer closure);
typedef void (* EFolderTreeForeachFunc) (EFolderTree *tree, const gchar *path, gpointer data, gpointer closure);

EFolderTree *e_folder_tree_new               (EFolderDestroyNotify    folder_destroy_notify,
					      void                   *closure);

void        e_folder_tree_destroy            (EFolderTree            *folder_tree);

gboolean    e_folder_tree_add                (EFolderTree            *folder_tree,
					      const gchar             *path,
					      void                   *data);
gboolean    e_folder_tree_remove             (EFolderTree            *folder_tree,
					      const gchar             *path);

gint         e_folder_tree_get_count          (EFolderTree            *folder_tree);

void       *e_folder_tree_get_folder         (EFolderTree            *folder_tree,
					      const gchar             *path);
GList      *e_folder_tree_get_subfolders     (EFolderTree            *folder_tree,
					      const gchar             *path);

void        e_folder_tree_foreach            (EFolderTree            *folder_tree,
					      EFolderTreeForeachFunc  foreach_func,
					      void                   *data);

const gchar *e_folder_tree_get_path_for_data  (EFolderTree            *folder_tree,
					      const void             *data);

#endif /* _E_FOLDER_TREE_H_ */

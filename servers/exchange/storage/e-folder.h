/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-folder.h
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

#ifndef _E_FOLDER_H_
#define _E_FOLDER_H_

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define E_TYPE_FOLDER			(e_folder_get_type ())
#define E_FOLDER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_FOLDER, EFolder))
#define E_FOLDER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_FOLDER, EFolderClass))
#define E_IS_FOLDER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_FOLDER))
#define E_IS_FOLDER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_FOLDER))
#define E_FOLDER_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_FOLDER, EFolderClass))

typedef struct EFolder        EFolder;
typedef struct EFolderPrivate EFolderPrivate;
typedef struct EFolderClass   EFolderClass;

struct EFolder {
	GObject parent;

	EFolderPrivate *priv;
};

struct EFolderClass {
	GObjectClass parent_class;

	/* Methods.  */
	gboolean (* accept_drop) (EFolder *folder,
				  GdkDragContext *context,
				  const gchar *target_type,
				  GtkSelectionData *selection_data);

	/* Signals.  */
	void (* changed) (EFolder *folder);
	void (* name_changed) (EFolder *folder);
};

GType    e_folder_get_type   (void);
void     e_folder_construct  (EFolder    *folder,
			      const gchar *name,
			      const gchar *type,
			      const gchar *description);
EFolder *e_folder_new        (const gchar *name,
			      const gchar *type,
			      const gchar *description);

const gchar *e_folder_get_name		  (EFolder *folder);
const gchar *e_folder_get_type_string	  (EFolder *folder);
const gchar *e_folder_get_description	  (EFolder *folder);
const gchar *e_folder_get_physical_uri	  (EFolder *folder);
gint         e_folder_get_unread_count	  (EFolder *folder);
gboolean    e_folder_get_highlighted	  (EFolder *folder);
gboolean    e_folder_get_is_stock	  (EFolder *folder);
gboolean    e_folder_get_can_sync_offline (EFolder *folder);
gboolean    e_folder_get_has_subfolders   (EFolder *folder);
const gchar *e_folder_get_custom_icon_name (EFolder *folder);
gint         e_folder_get_sorting_priority (EFolder *folder);

void  e_folder_set_name              (EFolder *folder, const gchar *name);
void  e_folder_set_type_string       (EFolder *folder, const gchar *type);
void  e_folder_set_description       (EFolder *folder, const gchar *description);
void  e_folder_set_physical_uri      (EFolder *folder, const gchar *physical_uri);
void  e_folder_set_unread_count      (EFolder *folder, gint unread_count);
void  e_folder_set_child_highlight   (EFolder *folder, gboolean highlighted);
void  e_folder_set_is_stock          (EFolder *folder, gboolean is_stock);
void  e_folder_set_can_sync_offline  (EFolder *folder, gboolean can_sync_offline);
void  e_folder_set_has_subfolders    (EFolder *folder, gboolean has_subfolders);
void  e_folder_set_custom_icon       (EFolder *folder, const gchar *icon_name);
void  e_folder_set_sorting_priority  (EFolder *folder, gint sorting_priority);

gboolean e_folder_accept_drop        (EFolder *folder, GdkDragContext *context,
				      const gchar *target_type,
				      GtkSelectionData *selection_data);
G_END_DECLS

#endif /* _E_FOLDER_H_ */

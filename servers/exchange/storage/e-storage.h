/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-storage.h
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

#ifndef _E_STORAGE_H_
#define _E_STORAGE_H_

#include <glib-object.h>
#include "e-folder.h"

G_BEGIN_DECLS

#define E_TYPE_STORAGE			(e_storage_get_type ())
#define E_STORAGE(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_STORAGE, EStorage))
#define E_STORAGE_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_STORAGE, EStorageClass))
#define E_IS_STORAGE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_STORAGE))
#define E_IS_STORAGE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_STORAGE))
#define E_STORAGE_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_STORAGE, EStorageClass))

typedef struct EStorage        EStorage;
typedef struct EStoragePrivate EStoragePrivate;
typedef struct EStorageClass   EStorageClass;

typedef enum {
	E_STORAGE_OK,
	E_STORAGE_GENERICERROR,
	E_STORAGE_EXISTS,
	E_STORAGE_INVALIDTYPE,
	E_STORAGE_IOERROR,
	E_STORAGE_NOSPACE,
	E_STORAGE_NOTEMPTY,
	E_STORAGE_NOTFOUND,
	E_STORAGE_NOTIMPLEMENTED,
	E_STORAGE_PERMISSIONDENIED,
	E_STORAGE_UNSUPPORTEDOPERATION,
	E_STORAGE_UNSUPPORTEDTYPE,
	E_STORAGE_CANTCHANGESTOCKFOLDER,
	E_STORAGE_CANTMOVETODESCENDANT,
	E_STORAGE_NOTONLINE,
	E_STORAGE_INVALIDNAME
} EStorageResult;

typedef void (* EStorageResultCallback) (EStorage *storage, EStorageResult result, gpointer data);
typedef void (* EStorageDiscoveryCallback) (EStorage *storage, EStorageResult result, const gchar *path, gpointer data);

struct EStorage {
	GObject parent;

	EStoragePrivate *priv;
};

struct EStorageClass {
	GObjectClass parent_class;

	/* Signals.  */

	void (* new_folder)     (EStorage *storage, const gchar *path);
	void (* updated_folder) (EStorage *storage, const gchar *path);
	void (* removed_folder) (EStorage *storage, const gchar *path);

	/* Virtual methods.  */

	GList      * (* get_subfolder_paths)     (EStorage *storage,
						  const gchar *path);
	EFolder    * (* get_folder)		 (EStorage *storage,
						  const gchar *path);
	const gchar * (* get_name)		 (EStorage *storage);

	void         (* async_create_folder)  (EStorage *storage,
					       const gchar *path,
					       const gchar *type,
					       EStorageResultCallback callback,
					       gpointer data);

	void         (* async_remove_folder)  (EStorage *storage,
					       const gchar *path,
					       EStorageResultCallback callback,
					       gpointer data);

	void         (* async_xfer_folder)    (EStorage *storage,
					       const gchar *source_path,
					       const gchar *destination_path,
					       const gboolean remove_source,
					       EStorageResultCallback callback,
					       gpointer data);

	void         (* async_open_folder)    (EStorage *storage,
					       const gchar *path,
					       EStorageDiscoveryCallback callback,
					       gpointer data);

	gboolean     (* will_accept_folder)   (EStorage *storage,
					       EFolder *new_parent,
					       EFolder *source);

	void         (* async_discover_shared_folder)  (EStorage *storage,
							const gchar *owner,
							const gchar *folder_name,
							EStorageDiscoveryCallback callback,
							gpointer data);
	void         (* cancel_discover_shared_folder) (EStorage *storage,
							const gchar *owner,
							const gchar *folder_name);
	void         (* async_remove_shared_folder)    (EStorage *storage,
							const gchar *path,
							EStorageResultCallback callback,
							gpointer data);
};

GType       e_storage_get_type                (void);
void        e_storage_construct               (EStorage   *storage,
					       const gchar *name,
					       EFolder    *root_folder);
EStorage   *e_storage_new                     (const gchar *name,
					       EFolder    *root_folder);

gboolean    e_storage_path_is_relative        (const gchar *path);
gboolean    e_storage_path_is_absolute        (const gchar *path);

GList      *e_storage_get_subfolder_paths     (EStorage   *storage,
					       const gchar *path);
EFolder    *e_storage_get_folder              (EStorage   *storage,
					       const gchar *path);

const gchar *e_storage_get_name                (EStorage *storage);

/* Folder operations.  */

void  e_storage_async_create_folder  (EStorage               *storage,
				      const gchar             *path,
				      const gchar             *type,
				      EStorageResultCallback  callback,
				      void                   *data);
void  e_storage_async_remove_folder  (EStorage               *storage,
				      const gchar             *path,
				      EStorageResultCallback  callback,
				      void                   *data);
void  e_storage_async_xfer_folder    (EStorage               *storage,
				      const gchar             *source_path,
				      const gchar             *destination_path,
				      const gboolean          remove_source,
				      EStorageResultCallback  callback,
				      void                   *data);
void  e_storage_async_open_folder    (EStorage                  *storage,
				      const gchar                *path,
				      EStorageDiscoveryCallback  callback,
				      void                      *data);

const gchar *e_storage_result_to_string   (EStorageResult  result);

gboolean    e_storage_will_accept_folder (EStorage       *storage,
					  EFolder        *new_parent,
					  EFolder        *source);

/* Shared folders.  */
void        e_storage_async_discover_shared_folder  (EStorage                 *storage,
						     const gchar               *owner,
						     const gchar               *folder_name,
						     EStorageDiscoveryCallback callback,
						     void                     *data);
void        e_storage_cancel_discover_shared_folder (EStorage                 *storage,
						     const gchar               *owner,
						     const gchar               *folder_name);
void        e_storage_async_remove_shared_folder    (EStorage                 *storage,
						     const gchar               *path,
						     EStorageResultCallback    callback,
						     void                     *data);

/* Utility functions.  */

gchar *e_storage_get_path_for_physical_uri  (EStorage   *storage,
					    const gchar *physical_uri);

/* FIXME: Need to rename these.  */

gboolean e_storage_new_folder             (EStorage   *storage,
					   const gchar *path,
					   EFolder    *folder);
gboolean e_storage_removed_folder         (EStorage   *storage,
					   const gchar *path);

gboolean e_storage_declare_has_subfolders (EStorage   *storage,
					   const gchar *path,
					   const gchar *message);
gboolean e_storage_get_has_subfolders     (EStorage   *storage,
					   const gchar *path);

G_END_DECLS

#endif /* _E_STORAGE_H_ */

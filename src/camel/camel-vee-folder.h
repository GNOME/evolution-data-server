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
 * Authors: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_VEE_FOLDER_H
#define CAMEL_VEE_FOLDER_H

#include <camel/camel-enums.h>
#include <camel/camel-folder.h>
#include <camel/camel-store.h>
#include <camel/camel-vee-summary.h>

/* Standard GObject macros */
#define CAMEL_TYPE_VEE_FOLDER \
	(camel_vee_folder_get_type ())
#define CAMEL_VEE_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_VEE_FOLDER, CamelVeeFolder))
#define CAMEL_VEE_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_VEE_FOLDER, CamelVeeFolderClass))
#define CAMEL_IS_VEE_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_VEE_FOLDER))
#define CAMEL_IS_VEE_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_VEE_FOLDER))
#define CAMEL_VEE_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_VEE_FOLDER, CamelVeeFolderClass))

G_BEGIN_DECLS

typedef struct _CamelVeeFolder CamelVeeFolder;
typedef struct _CamelVeeFolderClass CamelVeeFolderClass;
typedef struct _CamelVeeFolderPrivate CamelVeeFolderPrivate;

struct _CamelVeeFolder {
	CamelFolder parent;
	CamelVeeFolderPrivate *priv;
};

struct _CamelVeeFolderClass {
	CamelFolderClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_vee_folder_get_type		(void);
CamelFolder *	camel_vee_folder_new			(CamelStore *parent_store,
							 const gchar *full,
							 guint32 flags);
void		camel_vee_folder_construct		(CamelVeeFolder *vf,
							 guint32 flags);
guint32		camel_vee_folder_get_flags		(CamelVeeFolder *vf);
CamelFolder *	camel_vee_folder_get_location		(CamelVeeFolder *vf,
							 const CamelVeeMessageInfo *vinfo,
							 gchar **realuid);
CamelFolder *	camel_vee_folder_dup_vee_uid_folder	(CamelVeeFolder *vfolder,
							 const gchar *vee_message_uid);
void		camel_vee_folder_set_auto_update	(CamelVeeFolder *vfolder,
							 gboolean auto_update);
gboolean	camel_vee_folder_get_auto_update	(CamelVeeFolder *vfolder);
gboolean	camel_vee_folder_set_expression_sync	(CamelVeeFolder *vfolder,
							 const gchar *expression,
							 CamelVeeFolderOpFlags op_flags,
							 GCancellable *cancellable,
							 GError **error);
const gchar *	camel_vee_folder_get_expression		(CamelVeeFolder *vfolder);
gboolean	camel_vee_folder_add_folder_sync	(CamelVeeFolder *vfolder,
							 CamelFolder *subfolder,
							 CamelVeeFolderOpFlags op_flags,
							 GCancellable *cancellable,
							 GError **error);
gboolean	camel_vee_folder_remove_folder_sync	(CamelVeeFolder *vfolder,
							 CamelFolder *subfolder,
							 CamelVeeFolderOpFlags op_flags,
							 GCancellable *cancellable,
							 GError **error);
gboolean	camel_vee_folder_set_folders_sync	(CamelVeeFolder *vfolder,
							 GPtrArray *folders, /* CamelFolder * */
							 CamelVeeFolderOpFlags op_flags,
							 GCancellable *cancellable,
							 GError **error);
GPtrArray *	camel_vee_folder_dup_folders		(CamelVeeFolder *vfolder); /* CamelFolder * */

G_END_DECLS

#endif /* CAMEL_VEE_FOLDER_H */

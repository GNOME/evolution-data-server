/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * camel-disco-folder.h: Abstract class for a disconnectable folder
 *
 * Authors: Dan Winship <danw@ximian.com>
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_DISABLE_DEPRECATED

#ifndef CAMEL_DISCO_FOLDER_H
#define CAMEL_DISCO_FOLDER_H

#include <camel/camel-folder.h>

/* Standard GObject macros */
#define CAMEL_TYPE_DISCO_FOLDER \
	(camel_disco_folder_get_type ())
#define CAMEL_DISCO_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_DISCO_FOLDER, CamelDiscoFolder))
#define CAMEL_DISCO_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_DISCO_FOLDER, CamelDiscoFolderClass))
#define CAMEL_IS_DISCO_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_DISCO_FOLDER))
#define CAMEL_IS_DISCO_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_DISCO_FOLDER))
#define CAMEL_DISCO_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_DISCO_FOLDER, CamelDiscoFolderClass))

G_BEGIN_DECLS

typedef struct _CamelDiscoFolder CamelDiscoFolder;
typedef struct _CamelDiscoFolderClass CamelDiscoFolderClass;
typedef struct _CamelDiscoFolderPrivate CamelDiscoFolderPrivate;

struct _CamelDiscoFolder {
	CamelFolder parent;
	CamelDiscoFolderPrivate *priv;
};

struct _CamelDiscoFolderClass {
	CamelFolderClass parent_class;

	gboolean	(*refresh_info_online)	(CamelFolder *folder,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*sync_online)		(CamelFolder *folder,
						 GError **error);
	gboolean	(*sync_offline)		(CamelFolder *folder,
						 GError **error);
	gboolean	(*sync_resyncing)	(CamelFolder *folder,
						 GError **error);
	gboolean	(*expunge_uids_online)	(CamelFolder *folder,
						 GPtrArray *uids,
						 GError **error);
	gboolean	(*expunge_uids_offline)	(CamelFolder *folder,
						 GPtrArray *uids,
						 GError **error);
	gboolean	(*expunge_uids_resyncing)
						(CamelFolder *folder,
						 GPtrArray *uids,
						 GError **error);
	gboolean	(*append_online)	(CamelFolder *folder,
						 CamelMimeMessage *message,
						 const CamelMessageInfo *info,
						 gchar **appended_uid,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*append_offline)	(CamelFolder *folder,
						 CamelMimeMessage *message,
						 const CamelMessageInfo *info,
						 gchar **appended_uid,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*append_resyncing)	(CamelFolder *folder,
						 CamelMimeMessage *message,
						 const CamelMessageInfo *info,
						 gchar **appended_uid,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*transfer_online)	(CamelFolder *source,
						 GPtrArray *uids,
						 CamelFolder *destination,
						 GPtrArray **transferred_uids,
						 gboolean delete_originals,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*transfer_offline)	(CamelFolder *source,
						 GPtrArray *uids,
						 CamelFolder *destination,
						 GPtrArray **transferred_uids,
						 gboolean delete_originals,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*transfer_resyncing)	(CamelFolder *source,
						 GPtrArray *uids,
						 CamelFolder *destination,
						 GPtrArray **transferred_uids,
						 gboolean delete_originals,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*cache_message)	(CamelDiscoFolder *disco_folder,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*prepare_for_offline)	(CamelDiscoFolder *disco_folder,
						 const gchar *expression,
						 GCancellable *cancellable,
						 GError **error);
	void		(*update_uid)		(CamelFolder *folder,
						 const gchar *old_uid,
						 const gchar *new_uid);
};

GType		camel_disco_folder_get_type	(void);
gboolean	camel_disco_folder_get_offline_sync
						(CamelDiscoFolder *disco_folder);
void		camel_disco_folder_set_offline_sync
						(CamelDiscoFolder *disco_folder,
						 gboolean offline_sync);
gboolean	camel_disco_folder_expunge_uids	(CamelFolder *folder,
						 GPtrArray *uids,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_disco_folder_cache_message (CamelDiscoFolder *disco_folder,
						 const gchar *uid,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_disco_folder_prepare_for_offline
						(CamelDiscoFolder *disco_folder,
						 const gchar *expression,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_DISCO_FOLDER_H */

#endif /* CAMEL_DISABLE_DEPRECATED */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.h: class for an imap folder */

/*
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Jeffrey Stedfast <fejj@ximian.com>
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

#ifndef CAMEL_IMAP_FOLDER_H
#define CAMEL_IMAP_FOLDER_H

#include <camel/camel.h>

#include "camel-imap-command.h"
#include "camel-imap-message-cache.h"

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP_FOLDER \
	(camel_imap_folder_get_type ())
#define CAMEL_IMAP_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP_FOLDER, CamelImapFolder))
#define CAMEL_IMAP_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP_FOLDER, CamelImapFolderClass))
#define CAMEL_IS_IMAP_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP_FOLDER))
#define CAMEL_IS_IMAP_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP_FOLDER))
#define CAMEL_IMAP_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP_FOLDER, CamelImapFolderClass))

G_BEGIN_DECLS

struct _CamelIMAP4Journal;

typedef struct _CamelImapFolder CamelImapFolder;
typedef struct _CamelImapFolderClass CamelImapFolderClass;
typedef struct _CamelImapFolderPrivate CamelImapFolderPrivate;

struct _CamelImapFolder {
	CamelOfflineFolder parent;
	CamelImapFolderPrivate *priv;

	CamelFolderSearch *search;
	CamelImapMessageCache *cache;
	CamelOfflineJournal *journal;

	guint need_rescan : 1;
	guint need_refresh : 1;
	guint read_only : 1;
};

struct _CamelImapFolderClass {
	CamelOfflineFolderClass parent_class;
};

GType		camel_imap_folder_get_type	(void);
CamelFolder *	camel_imap_folder_new		(CamelStore *parent,
						 const gchar *folder_name,
						 const gchar *folder_dir,
						 GError **error);
gboolean	camel_imap_folder_get_check_folder
						(CamelImapFolder *imap_folder);
void		camel_imap_folder_set_check_folder
						(CamelImapFolder *imap_folder,
						 gboolean check_folder);
gboolean	camel_imap_folder_selected	(CamelFolder *folder,
						 CamelImapResponse *response,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imap_folder_changed	(CamelFolder *folder,
						 gint exists,
						 GArray *expunged,
						 GCancellable *cancellable,
						 GError **error);
CamelStream *	camel_imap_folder_fetch_data	(CamelImapFolder *imap_folder,
						 const gchar *uid,
						 const gchar *section_text,
						 gboolean cache_only,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imap_append_resyncing	(CamelFolder *folder,
						 CamelMimeMessage *message,
						 CamelMessageInfo *info,
						 gchar **appended_uid,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imap_transfer_resyncing	(CamelFolder *source,
						 GPtrArray *uids,
						 CamelFolder *dest,
						 gboolean delete_originals,
						 GPtrArray **transferred_uids,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imap_expunge_uids_resyncing
						(CamelFolder *folder,
						 GPtrArray *uids,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_imap_expunge_uids_only	(CamelFolder *folder,
						 GPtrArray *uids,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_IMAP_FOLDER_H */

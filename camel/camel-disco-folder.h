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

#ifndef CAMEL_DISABLE_DEPRECATED

#ifndef CAMEL_DISCO_FOLDER_H
#define CAMEL_DISCO_FOLDER_H 1

#include "camel-folder.h"

#define CAMEL_DISCO_FOLDER_TYPE     (camel_disco_folder_get_type ())
#define CAMEL_DISCO_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_DISCO_FOLDER_TYPE, CamelDiscoFolder))
#define CAMEL_DISCO_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_DISCO_FOLDER_TYPE, CamelDiscoFolderClass))
#define CAMEL_IS_DISCO_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_DISCO_FOLDER_TYPE))

G_BEGIN_DECLS

enum {
	CAMEL_DISCO_FOLDER_ARG_OFFLINE_SYNC = CAMEL_FOLDER_ARG_LAST,
	CAMEL_DISCO_FOLDER_ARG_LAST = CAMEL_FOLDER_ARG_LAST + 0x100
};

enum {
	CAMEL_DISCO_FOLDER_OFFLINE_SYNC = CAMEL_DISCO_FOLDER_ARG_OFFLINE_SYNC | CAMEL_ARG_BOO
};

struct _CamelDiscoFolder {
	CamelFolder parent_object;

	guint offline_sync:1;
};

typedef struct {
	CamelFolderClass parent_class;

	void (*refresh_info_online) (CamelFolder *folder, CamelException *ex);

	void (*sync_online)    (CamelFolder *folder, CamelException *ex);
	void (*sync_offline)   (CamelFolder *folder, CamelException *ex);
	void (*sync_resyncing) (CamelFolder *folder, CamelException *ex);

	void (*expunge_uids_online)    (CamelFolder *folder, GPtrArray *uids,
					CamelException *ex);
	void (*expunge_uids_offline)   (CamelFolder *folder, GPtrArray *uids,
					CamelException *ex);
	void (*expunge_uids_resyncing) (CamelFolder *folder, GPtrArray *uids,
					CamelException *ex);

	void (*append_online)    (CamelFolder *folder,
				  CamelMimeMessage *message,
				  const CamelMessageInfo *info,
				  gchar **appended_uid,
				  CamelException *ex);
	void (*append_offline)   (CamelFolder *folder,
				  CamelMimeMessage *message,
				  const CamelMessageInfo *info,
				  gchar **appended_uid,
				  CamelException *ex);
	void (*append_resyncing) (CamelFolder *folder,
				  CamelMimeMessage *message,
				  const CamelMessageInfo *info,
				  gchar **appended_uid,
				  CamelException *ex);

	void (*transfer_online)    (CamelFolder *source, GPtrArray *uids,
				    CamelFolder *destination,
				    GPtrArray **transferred_uids,
				    gboolean delete_originals,
				    CamelException *ex);
	void (*transfer_offline)   (CamelFolder *source, GPtrArray *uids,
				    CamelFolder *destination,
				    GPtrArray **transferred_uids,
				    gboolean delete_originals,
				    CamelException *ex);
	void (*transfer_resyncing) (CamelFolder *source, GPtrArray *uids,
				    CamelFolder *destination,
				    GPtrArray **transferred_uids,
				    gboolean delete_originals,
				    CamelException *ex);

	void (*cache_message)       (CamelDiscoFolder *disco_folder,
				     const gchar *uid, CamelException *ex);
	void (*prepare_for_offline) (CamelDiscoFolder *disco_folder,
				     const gchar *expression,
				     CamelException *ex);

	void (*update_uid) (CamelFolder *folder, const gchar *old_uid,
			    const gchar *new_uid);
} CamelDiscoFolderClass;

/* public methods */
void camel_disco_folder_expunge_uids (CamelFolder *folder, GPtrArray *uids,
				      CamelException *ex);

void camel_disco_folder_cache_message       (CamelDiscoFolder *disco_folder,
					     const gchar *uid,
					     CamelException *ex);
void camel_disco_folder_prepare_for_offline (CamelDiscoFolder *disco_folder,
					     const gchar *expression,
					     CamelException *ex);

/* Standard Camel function */
CamelType camel_disco_folder_get_type (void);

G_END_DECLS

#endif /* CAMEL_DISCO_FOLDER_H */

#endif /* CAMEL_DISABLE_DEPRECATED */

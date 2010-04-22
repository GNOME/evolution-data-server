/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-folder.h: class for an groupwise folder */

/*
 * Authors:
 *   Sivaiah Nallagatla <snallagatla@novell.com>
 *   parthasarathi susarla <sparthasarathi@novell.com>
 *
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

#ifndef CAMEL_GROUPWISE_FOLDER_H
#define CAMEL_GROUPWISE_FOLDER_H

#include <camel/camel.h>

#include "camel-groupwise-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_GROUPWISE_FOLDER \
	(camel_groupwise_folder_get_type ())
#define CAMEL_GROUPWISE_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_GROUPWISE_FOLDER, CamelGroupwiseFolder))
#define CAMEL_GROUPWISE_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_GROUPWISE_FOLDER, CamelGroupwiseFolderClass))
#define CAMEL_IS_GROUPWISE_FOLDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_GROUPWISE_FOLDER))
#define CAMEL_IS_GROUPWISE_FOLDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_GROUPWISE_FOLDER))
#define CAMEL_GROUPWISE_FOLDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_GROUPWISE_FOLDER, CamelGroupwiseFolderClass))

G_BEGIN_DECLS

typedef struct _CamelGroupwiseFolder CamelGroupwiseFolder;
typedef struct _CamelGroupwiseFolderClass CamelGroupwiseFolderClass;
typedef struct _CamelGroupwiseFolderPrivate CamelGroupwiseFolderPrivate;

struct _CamelGroupwiseFolder {
	CamelOfflineFolder parent;
	CamelGroupwiseFolderPrivate *priv;

	CamelFolderSearch *search;

	CamelOfflineJournal *journal;
	CamelDataCache *cache;

	guint need_rescan:1;
	guint need_refresh:1;
	guint read_only:1;

};

struct _CamelGroupwiseFolderClass {
	CamelOfflineFolderClass parent_class;

	/* Virtual methods */

} ;

GType camel_groupwise_folder_get_type (void);

/* implemented */
CamelFolder * camel_gw_folder_new(CamelStore *store, const gchar *folder_dir, const gchar *folder_name, CamelException *ex);
void gw_update_summary ( CamelFolder *folder, GList *item_list,CamelException *ex);

G_END_DECLS

#endif /* CAMEL_GROUPWISE_FOLDER_H */

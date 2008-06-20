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
#define CAMEL_GROUPWISE_FOLDER_H 1

#include <camel/camel-offline-folder.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-folder-summary.h>
#include <camel/camel-internet-address.h>
#include <camel/camel-data-cache.h>
#include <camel/camel-multipart.h>
#include <camel/camel-multipart-signed.h>
#include <camel/camel-multipart-encrypted.h>
#include <camel/camel-offline-journal.h>

#include "camel-groupwise-summary.h"

#define CAMEL_GROUPWISE_FOLDER_TYPE     (camel_groupwise_folder_get_type ())
#define CAMEL_GROUPWISE_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_GROUPWISE_FOLDER_TYPE, CamelGroupwiseFolder))
#define CAMEL_GROUPWISE_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_GROUPWISE_FOLDER_TYPE, CamelGroupwiseFolderClass))
#define CAMEL_IS_GROUPWISE_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_GROUPWISE_FOLDER_TYPE))

G_BEGIN_DECLS

typedef struct  _CamelGroupwiseFolder CamelGroupwiseFolder;
typedef struct  _CamelGroupwiseFolderClass CamelGroupwiseFolderClass;
struct _CamelGroupwiseFolder {
	CamelOfflineFolder parent_object;

	struct _CamelGroupwiseFolderPrivate *priv;

	CamelFolderSearch *search;

	CamelOfflineJournal *journal;
	CamelDataCache *cache;

	unsigned int need_rescan:1;
	unsigned int need_refresh:1;
	unsigned int read_only:1;


};

struct _CamelGroupwiseFolderClass {
	CamelOfflineFolderClass parent_class;

	/* Virtual methods */	
	
} ;


/* Standard Camel function */
CamelType camel_groupwise_folder_get_type (void);

/* implemented */
CamelFolder * camel_gw_folder_new(CamelStore *store, const char *folder_dir, const char *folder_name, CamelException *ex) ;
void gw_update_summary ( CamelFolder *folder, GList *item_list,CamelException *ex) ;
void groupwise_refresh_folder(CamelFolder *folder, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_GROUPWISE_FOLDER_H */

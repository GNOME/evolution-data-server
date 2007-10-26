/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Johnny Jacob <jjohnny@novell.com>
 *   
 * Copyright (C) 2007, Novell Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 3 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __MAPI_FOLDER_H__
#define __MAPI_FOLDER_H__


#include <camel/camel-folder.h>
#include <camel/camel-offline-folder.h>
#include <camel/camel-data-cache.h>
#include <camel/camel-offline-folder.h>
#include <camel/camel-offline-journal.h>

#include "oc_thread.h"

#define PATH_FOLDER ".evolution/mail/openchange"

#define CAMEL_MAPI_FOLDER_TYPE     (camel_mapi_folder_get_type ())
#define CAMEL_MAPI_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MAPI_FOLDER_TYPE, CamelMapiFolder))
#define CAMEL_MAPI_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MAPI_FOLDER_TYPE, CamelMapiFolderClass))
#define CAMEL_IS_MAPI_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MAPI_FOLDER_TYPE))

/**
 * DATA STRUCTURES
 */

G_BEGIN_DECLS

typedef struct  _CamelMapiFolder CamelMapiFolder;
typedef struct  _CamelMapiFolderClass CamelMapiFolderClass;

struct _CamelMapiFolder {
	CamelOfflineFolder parent_object;

	struct _CamelMapiFolderPrivate *priv;

	CamelFolderSearch *search;

	CamelOfflineJournal *journal;
	CamelDataCache *cache;

	unsigned int need_rescan:1;
	unsigned int need_refresh:1;
	unsigned int read_only:1;
};

struct _CamelMapiFolderClass {
	CamelOfflineFolderClass parent_class;

	/* Virtual methods */	
	
} ;


/* Standard Camel function */
CamelType camel_mapi_folder_get_type (void);

/* implemented */
CamelFolder * camel_gw_folder_new(CamelStore *store, const char *folder_dir, const char *folder_name, CamelException *ex) ;
void gw_update_summary ( CamelFolder *folder, GList *item_list,CamelException *ex) ;
void mapi_refresh_folder(CamelFolder *folder, CamelException *ex);


/* -----------CLEANUP - JUST FOR COMPILING ----------------------------------- */
typedef struct {
	CamelFolder		folder;
	CamelOfflineFolder	parent_object;
  	CamelFolderSearch	*search;
  	CamelOfflineJournal	*journal;
	CamelDataCache		*cache;
	openchange_thread_t	*oc_thread;
  	char			*cachedir;
  	char			*folder_uid;
	char			*utf7_name;
  	unsigned int		read_only:1;
	unsigned int		enable_mlist:1;
	GHashTable		*modified; /* use to update server from summary */
	GPtrArray		*modified_key; /* a key table for hash table */
	int			n_modified;
	char			*folder_id;
} CamelOpenchangeFolder;

/**
 * PROTOTYPES
 */

#ifndef __BEGIN_DECLS
#ifdef __cplusplus
#define __BEGIN_DECLS		extern "C" {
#define __END_DECLS		}
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#endif

__BEGIN_DECLS
CamelFolder		*camel_openchange_folder_new(CamelStore *parent_store, const char *full_name, guint32 flags, CamelException *ex);
CamelMessageInfo	*openchange_get_message_info(CamelFolder *folder, const char *uid);
__END_DECLS

G_END_DECLS

#endif /* CAMEL_GROUPWISE_FOLDER_H */

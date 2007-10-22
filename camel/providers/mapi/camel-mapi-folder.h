/*
 *  Copyright (C) Jean-Baptiste Arnoult 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __OPENCHANGE_FOLDER_H__
#define __OPENCHANGE_FOLDER_H__


#include <camel/camel-folder.h>
#include <camel/camel-offline-folder.h>
#include <camel/camel-data-cache.h>
#include <camel/camel-offline-folder.h>
#include <camel/camel-offline-journal.h>

#include "oc_thread.h"

#define PATH_FOLDER ".evolution/mail/openchange"

/**
 * DATA STRUCTURES
 */

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

#endif /* !__OPENCHANGE_FOLDER_H__ */


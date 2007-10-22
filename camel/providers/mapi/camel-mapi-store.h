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

/* camel-exchange-store.h: class for a openchange store */


#ifndef __CAMEL_OPENCHANGE_STORE_H__
#define __CAMEL_OPENCHANGE_STORE_H__

#include <camel/camel-store.h>
#include <camel/camel-offline-store.h>
#include <camel-mapi-store-summary.h>
#include <camel/camel-net-utils.h>
#include <camel/camel-i18n.h>

#define FILE_NAME "/home/jibijoggs/tmp/eplugin/trunk/debug"
#define OC_DEBUG(t) \
{\
gchar *oc_debug_defined;\
oc_debug_defined = g_strdup_printf("echo %s >> %s;", t, FILE_NAME);\
system(oc_debug_defined);\
g_free(oc_debug_defined);\
}\



/**
 * DATA STRUCTURES
 */

/**
 * definition of CamelOpenchangeStore
 */
typedef struct {
	CamelStore		parent_object;	
/* 	CamelStub		*stub; */
	ocStoreSummary_t	*summary;
	char			*storage_path;
	char			*base_url;
	CamelURL		*camel_url;
	CamelFolderInfo		*fi;
	char			*trash_name;
	GHashTable		*folders;
	GMutex			*folders_lock;
	gboolean		stub_connected;
	GMutex			*connect_lock;
} CamelOpenchangeStore;



typedef struct {
	CamelOfflineStoreClass		parent_class;
} CamelOpenchangeStoreClass;


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
/* Standard Camel function */
CamelType camel_openchange_store_get_type(void);
gboolean camel_openchange_store_connected(CamelOpenchangeStore *, CamelException *);

/* camel-openchange-provider.c */
int m_oc_initialize(void);
__END_DECLS

#endif /* __CAMEL_OPENCHANGE_STORE_H__ */

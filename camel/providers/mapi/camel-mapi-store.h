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

/* camel-mapi-store.h: class for a openchange store */


#ifndef __CAMEL_MAPI_STORE_H__
#define __CAMEL_MAPI_STORE_H__

#include <camel/camel-store.h>
#include <camel/camel-offline-store.h>
#include <camel-mapi-store-summary.h>
#include <camel/camel-net-utils.h>
#include <camel/camel-i18n.h>

#include <exchange-mapi-folder.h>

#define OC_DEBUG(t) 

#define CAMEL_MAPI_STORE_TYPE     (camel_mapi_store_get_type ())
#define CAMEL_MAPI_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MAPI_STORE_TYPE, CamelMapiStore))
#define CAMEL_MAPI_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MAPI_STORE_TYPE, CamelMapiStoreClass))
#define CAMEL_IS_MAPI_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MAPI_STORE_TYPE))


/**
 * definition of CamelMAPIStore
 */
typedef struct _CamelMapiStore CamelMapiStore;
typedef struct _CamelMapiStoreClass CamelMapiStoreClass;
typedef struct _CamelMapiStorePrivate CamelMapiStorePrivate;

struct _CamelMapiStore{
	CamelOfflineStore parent_object;	

	struct _CamelMapiStoreSummary *summary;
	CamelMapiStorePrivate *priv;
/* 	ocStoreSummary_t	*summary; */
/* 	char			*base_url; */
/* 	CamelURL		*camel_url; */
/* 	CamelFolderInfo		*fi; */
/* 	GHashTable		*folders; */
/* 	GMutex			*folders_lock; */
/* 	GMutex			*connect_lock; */
};




struct _CamelMapiStoreClass {
	CamelOfflineStoreClass		parent_class;
};


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
CamelType camel_mapi_store_get_type(void);
gboolean camel_mapi_store_connected(CamelMapiStore *, CamelException *);

/* camel-openchange-provider.c */
int mapi_initialize(void);
__END_DECLS

#endif /* __CAMEL_OPENCHANGE_STORE_H__ */

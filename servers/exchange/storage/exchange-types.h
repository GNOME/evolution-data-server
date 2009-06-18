/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#include "e2k-types.h"
#include <glib-object.h>

#ifndef __EXCHANGE_TYPES_H__
#define __EXCHANGE_TYPES_H__

typedef struct _ExchangeAccount                  ExchangeAccount;
typedef struct _ExchangeAccountPrivate           ExchangeAccountPrivate;
typedef struct _ExchangeAccountClass             ExchangeAccountClass;
typedef struct _ExchangeConfigListener           ExchangeConfigListener;
typedef struct _ExchangeConfigListenerPrivate    ExchangeConfigListenerPrivate;
typedef struct _ExchangeConfigListenerClass      ExchangeConfigListenerClass;
typedef struct _ExchangeHierarchy                ExchangeHierarchy;
typedef struct _ExchangeHierarchyPrivate         ExchangeHierarchyPrivate;
typedef struct _ExchangeHierarchyClass           ExchangeHierarchyClass;
typedef struct _ExchangeHierarchyFavorites        ExchangeHierarchyFavorites;
typedef struct _ExchangeHierarchyFavoritesPrivate ExchangeHierarchyFavoritesPrivate;
typedef struct _ExchangeHierarchyFavoritesClass   ExchangeHierarchyFavoritesClass;
typedef struct _ExchangeHierarchyGAL             ExchangeHierarchyGAL;
typedef struct _ExchangeHierarchyGALPrivate      ExchangeHierarchyGALPrivate;
typedef struct _ExchangeHierarchyGALClass        ExchangeHierarchyGALClass;
typedef struct _ExchangeHierarchySomeDAV         ExchangeHierarchySomeDAV;
typedef struct _ExchangeHierarchySomeDAVPrivate  ExchangeHierarchySomeDAVPrivate;
typedef struct _ExchangeHierarchySomeDAVClass    ExchangeHierarchySomeDAVClass;
typedef struct _ExchangeHierarchyWebDAV          ExchangeHierarchyWebDAV;
typedef struct _ExchangeHierarchyWebDAVPrivate   ExchangeHierarchyWebDAVPrivate;
typedef struct _ExchangeHierarchyWebDAVClass     ExchangeHierarchyWebDAVClass;
typedef struct _ExchangeOfflineHandler           ExchangeOfflineHandler;
typedef struct _ExchangeOfflineHandlerClass      ExchangeOfflineHandlerClass;
typedef struct _ExchangePermissionsDialog        ExchangePermissionsDialog;
typedef struct _ExchangePermissionsDialogPrivate ExchangePermissionsDialogPrivate;
typedef struct _ExchangePermissionsDialogClass   ExchangePermissionsDialogClass;
typedef struct _ExchangeStorage                  ExchangeStorage;
typedef struct _ExchangeStoragePrivate           ExchangeStoragePrivate;
typedef struct _ExchangeStorageClass             ExchangeStorageClass;

typedef struct _EFolderExchange                  EFolderExchange;
typedef struct _EFolderExchangePrivate           EFolderExchangePrivate;
typedef struct _EFolderExchangeClass             EFolderExchangeClass;

typedef struct  XCBackend                        XCBackend;
typedef struct  XCBackendPrivate                 XCBackendPrivate;
typedef struct  XCBackendClass                   XCBackendClass;

typedef struct  XCBackendComponent               XCBackendComponent;
typedef struct  XCBackendComponentPrivate        XCBackendComponentPrivate;
typedef struct  XCBackendComponentClass          XCBackendComponentClass;

typedef struct  XCBackendView		 XCBackendView;
typedef struct  XCBackendViewPrivate		 XCBackendViewPrivate;
typedef struct  XCBackendViewClass		 XCBackendViewClass;

typedef enum {
	EXCHANGE_HIERARCHY_PERSONAL,
	EXCHANGE_HIERARCHY_FAVORITES,
	EXCHANGE_HIERARCHY_PUBLIC,
	EXCHANGE_HIERARCHY_GAL,
	EXCHANGE_HIERARCHY_FOREIGN
} ExchangeHierarchyType;

G_END_DECLS

#endif /* __EXCHANGE_TYPES_H__ */

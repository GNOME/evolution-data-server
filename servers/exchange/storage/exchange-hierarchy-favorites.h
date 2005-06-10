/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_HIERARCHY_FAVORITES_H__
#define __EXCHANGE_HIERARCHY_FAVORITES_H__

#include "exchange-hierarchy-somedav.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_HIERARCHY_FAVORITES            (exchange_hierarchy_favorites_get_type ())
#define EXCHANGE_HIERARCHY_FAVORITES(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_HIERARCHY_FAVORITES, ExchangeHierarchyFavorites))
#define EXCHANGE_HIERARCHY_FAVORITES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_HIERARCHY_FAVORITES, ExchangeHierarchyFavoritesClass))
#define EXCHANGE_IS_HIERARCHY_FAVORITES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_FAVORITES))
#define EXCHANGE_IS_HIERARCHY_FAVORITES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_FAVORITES))

struct _ExchangeHierarchyFavorites {
	ExchangeHierarchySomeDAV parent;

	ExchangeHierarchyFavoritesPrivate *priv;
};

struct _ExchangeHierarchyFavoritesClass {
	ExchangeHierarchySomeDAVClass parent_class;

};

GType              exchange_hierarchy_favorites_get_type (void);

ExchangeHierarchy *exchange_hierarchy_favorites_new (ExchangeAccount *account,
						     const char *hierarchy_name,
						     const char *physical_uri_prefix,
						     const char *home_uri,
						     const char *public_uri,
						     const char *owner_name,
						     const char *owner_email,
						     const char *source_uri);

ExchangeAccountFolderResult  exchange_hierarchy_favorites_add_folder (ExchangeHierarchy *hier,
								      EFolder *folder);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_HIERARCHY_FAVORITES_H__ */

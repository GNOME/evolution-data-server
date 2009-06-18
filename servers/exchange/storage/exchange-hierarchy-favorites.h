/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_HIERARCHY_FAVORITES_H__
#define __EXCHANGE_HIERARCHY_FAVORITES_H__

#include "exchange-hierarchy-somedav.h"

G_BEGIN_DECLS

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
						     const gchar *hierarchy_name,
						     const gchar *physical_uri_prefix,
						     const gchar *home_uri,
						     const gchar *public_uri,
						     const gchar *owner_name,
						     const gchar *owner_email,
						     const gchar *source_uri);

ExchangeAccountFolderResult  exchange_hierarchy_favorites_add_folder (ExchangeHierarchy *hier,
								      EFolder *folder);

gboolean exchange_hierarchy_favorites_is_added (ExchangeHierarchy *hier,
						EFolder *folder);

G_END_DECLS

#endif /* __EXCHANGE_HIERARCHY_FAVORITES_H__ */

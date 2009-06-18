/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_HIERARCHY_FOREIGN_H__
#define __EXCHANGE_HIERARCHY_FOREIGN_H__

#include "exchange-hierarchy-somedav.h"

G_BEGIN_DECLS

#define EXCHANGE_TYPE_HIERARCHY_FOREIGN            (exchange_hierarchy_foreign_get_type ())
#define EXCHANGE_HIERARCHY_FOREIGN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_HIERARCHY_FOREIGN, ExchangeHierarchyForeign))
#define EXCHANGE_HIERARCHY_FOREIGN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_HIERARCHY_FOREIGN, ExchangeHierarchyForeignClass))
#define EXCHANGE_IS_HIERARCHY_FOREIGN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_FOREIGN))
#define EXCHANGE_IS_HIERARCHY_FOREIGN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_FOREIGN))

typedef struct _ExchangeHierarchyForeignPrivate  ExchangeHierarchyForeignPrivate;

struct _ExchangeHierarchyForeign {
	ExchangeHierarchySomeDAV parent;

	ExchangeHierarchyForeignPrivate *priv;
};

struct _ExchangeHierarchyForeignClass {
	ExchangeHierarchySomeDAVClass parent_class;

};

typedef struct _ExchangeHierarchyForeignClass ExchangeHierarchyForeignClass;
typedef struct _ExchangeHierarchyForeign ExchangeHierarchyForeign;

GType              exchange_hierarchy_foreign_get_type (void);

ExchangeHierarchy *exchange_hierarchy_foreign_new          (ExchangeAccount *account,
							    const gchar *hierarchy_name,
							    const gchar *physical_uri_prefix,
							    const gchar *internal_uri_prefix,
							    const gchar *owner_name,
							    const gchar *owner_email,
							    const gchar *source_uri);
ExchangeHierarchy *exchange_hierarchy_foreign_new_from_dir (ExchangeAccount *account,
							    const gchar *folder_path);

ExchangeAccountFolderResult  exchange_hierarchy_foreign_add_folder (ExchangeHierarchy *hier,
								    const gchar *folder_name,
								    EFolder **folder);

G_END_DECLS

#endif /* __EXCHANGE_HIERARCHY_FOREIGN_H__ */

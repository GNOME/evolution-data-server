/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_HIERARCHY_H__
#define __EXCHANGE_HIERARCHY_H__

#include "exchange-types.h"
#include "exchange-account.h"
#include "e-folder.h"

G_BEGIN_DECLS

#define EXCHANGE_TYPE_HIERARCHY            (exchange_hierarchy_get_type ())
#define EXCHANGE_HIERARCHY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_HIERARCHY, ExchangeHierarchy))
#define EXCHANGE_HIERARCHY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_HIERARCHY, ExchangeHierarchyClass))
#define EXCHANGE_IS_HIERARCHY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY))
#define EXCHANGE_IS_HIERARCHY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY))

struct _ExchangeHierarchy {
	GObject parent;

	ExchangeAccount *account;
	ExchangeHierarchyType type;
	EFolder *toplevel;

	gchar *owner_name;
	gchar *owner_email;
	gchar *source_uri;

	gboolean hide_private_items;
};

struct _ExchangeHierarchyClass {
	GObjectClass parent_class;

	/* methods */
	gboolean (*is_empty) (ExchangeHierarchy *hier);

	void (*add_to_storage) (ExchangeHierarchy *hier);
	void (*rescan) (ExchangeHierarchy *hier);
	ExchangeAccountFolderResult (*scan_subtree)  (ExchangeHierarchy *hier,
						      EFolder *folder,
						      gint mode);

	ExchangeAccountFolderResult (*create_folder) (ExchangeHierarchy *hier,
						      EFolder *parent,
						      const gchar *name,
						      const gchar *type);
	ExchangeAccountFolderResult (*remove_folder) (ExchangeHierarchy *hier,
						      EFolder *folder);
	ExchangeAccountFolderResult (*xfer_folder)   (ExchangeHierarchy *hier,
						      EFolder *source,
						      EFolder *dest_parent,
						      const gchar *dest_name,
						      gboolean remove_source);

	/* signals */
	void (*new_folder)     (ExchangeHierarchy *hier,
				EFolder *folder);
	void (*removed_folder) (ExchangeHierarchy *hier,
				EFolder *folder);
};

GType    exchange_hierarchy_get_type            (void);

void     exchange_hierarchy_construct           (ExchangeHierarchy *hier,
						 ExchangeAccount   *account,
						 ExchangeHierarchyType type,
						 EFolder           *toplevel,
						 const gchar        *owner_name,
						 const gchar        *owner_email,
						 const gchar        *source_uri);

void     exchange_hierarchy_new_folder          (ExchangeHierarchy *hier,
						 EFolder           *folder);
void     exchange_hierarchy_removed_folder      (ExchangeHierarchy *hier,
						 EFolder           *folder);

gboolean exchange_hierarchy_is_empty            (ExchangeHierarchy *hier);

void                        exchange_hierarchy_add_to_storage (ExchangeHierarchy *hier);
void                        exchange_hierarchy_rescan         (ExchangeHierarchy *hier);
ExchangeAccountFolderResult exchange_hierarchy_scan_subtree   (ExchangeHierarchy *hier,
							       EFolder           *folder,
							       gint		  mode);

ExchangeAccountFolderResult exchange_hierarchy_create_folder (ExchangeHierarchy *hier,
							      EFolder           *parent,
							      const gchar        *name,
							      const gchar        *type);
ExchangeAccountFolderResult exchange_hierarchy_remove_folder (ExchangeHierarchy *hier,
							      EFolder           *folder);
ExchangeAccountFolderResult exchange_hierarchy_xfer_folder   (ExchangeHierarchy *hier,
							      EFolder           *source,
							      EFolder           *dest_parent,
							      const gchar        *dest_name,
							      gboolean           remove_source);

G_END_DECLS

#endif /* __EXCHANGE_HIERARCHY_H__ */

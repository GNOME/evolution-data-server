/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_HIERARCHY_H__
#define __EXCHANGE_HIERARCHY_H__

#include "exchange-types.h"
#include "exchange-account.h"
#include "e-folder.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_HIERARCHY            (exchange_hierarchy_get_type ())
#define EXCHANGE_HIERARCHY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_HIERARCHY, ExchangeHierarchy))
#define EXCHANGE_HIERARCHY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_HIERARCHY, ExchangeHierarchyClass))
#define EXCHANGE_IS_HIERARCHY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY))
#define EXCHANGE_IS_HIERARCHY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY))

typedef enum {
	EXCHANGE_HIERARCHY_PERSONAL,
	EXCHANGE_HIERARCHY_FAVORITES,
	EXCHANGE_HIERARCHY_PUBLIC,
	EXCHANGE_HIERARCHY_GAL,
	EXCHANGE_HIERARCHY_FOREIGN
} ExchangeHierarchyType;

struct _ExchangeHierarchy {
	GObject parent;

	ExchangeAccount *account;
	ExchangeHierarchyType type;
	EFolder *toplevel;

	char *owner_name;
	char *owner_email;
	char *source_uri;

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
						      gboolean offline);

	ExchangeAccountFolderResult (*create_folder) (ExchangeHierarchy *hier,
						      EFolder *parent,
						      const char *name,
						      const char *type);
	ExchangeAccountFolderResult (*remove_folder) (ExchangeHierarchy *hier,
						      EFolder *folder);
	ExchangeAccountFolderResult (*xfer_folder)   (ExchangeHierarchy *hier,
						      EFolder *source,
						      EFolder *dest_parent,
						      const char *dest_name,
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
						 const char        *owner_name,
						 const char        *owner_email,
						 const char        *source_uri);

void     exchange_hierarchy_new_folder          (ExchangeHierarchy *hier,
						 EFolder           *folder);
void     exchange_hierarchy_removed_folder      (ExchangeHierarchy *hier,
						 EFolder           *folder);

gboolean exchange_hierarchy_is_empty            (ExchangeHierarchy *hier);

void                        exchange_hierarchy_add_to_storage (ExchangeHierarchy *hier);
void                        exchange_hierarchy_rescan         (ExchangeHierarchy *hier);
ExchangeAccountFolderResult exchange_hierarchy_scan_subtree   (ExchangeHierarchy *hier,
							       EFolder           *folder,
							       gboolean		  offline);

ExchangeAccountFolderResult exchange_hierarchy_create_folder (ExchangeHierarchy *hier,
							      EFolder           *parent,
							      const char        *name,
							      const char        *type);
ExchangeAccountFolderResult exchange_hierarchy_remove_folder (ExchangeHierarchy *hier,
							      EFolder           *folder);
ExchangeAccountFolderResult exchange_hierarchy_xfer_folder   (ExchangeHierarchy *hier,
							      EFolder           *source,
							      EFolder           *dest_parent,
							      const char        *dest_name,
							      gboolean           remove_source);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_HIERARCHY_H__ */

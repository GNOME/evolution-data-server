/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_HIERARCHY_WEBDAV_H__
#define __EXCHANGE_HIERARCHY_WEBDAV_H__

#include "exchange-hierarchy.h"
/* #include "exchange-folder-size.h" */

G_BEGIN_DECLS

#define EXCHANGE_TYPE_HIERARCHY_WEBDAV            (exchange_hierarchy_webdav_get_type ())
#define EXCHANGE_HIERARCHY_WEBDAV(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_HIERARCHY_WEBDAV, ExchangeHierarchyWebDAV))
#define EXCHANGE_HIERARCHY_WEBDAV_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_HIERARCHY_WEBDAV, ExchangeHierarchyWebDAVClass))
#define EXCHANGE_IS_HIERARCHY_WEBDAV(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_WEBDAV))
#define EXCHANGE_IS_HIERARCHY_WEBDAV_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_WEBDAV))

struct _ExchangeHierarchyWebDAV {
	ExchangeHierarchy parent;

	ExchangeHierarchyWebDAVPrivate *priv;
};

struct _ExchangeHierarchyWebDAVClass {
	ExchangeHierarchyClass parent_class;

};

GType              exchange_hierarchy_webdav_get_type (void);

ExchangeHierarchy *exchange_hierarchy_webdav_new (ExchangeAccount *account,
						  ExchangeHierarchyType type,
						  const gchar *hierarchy_name,
						  const gchar *physical_uri_prefix,
						  const gchar *internal_uri_prefix,
						  const gchar *owner_name,
						  const gchar *owner_email,
						  const gchar *source_uri,
						  gboolean deep_searchable);

/* for subclasses */
ExchangeAccountFolderResult exchange_hierarchy_webdav_status_to_folder_result (E2kHTTPStatus status);
EFolder *exchange_hierarchy_webdav_parse_folder (ExchangeHierarchyWebDAV *hwd,
						 EFolder *parent,
						 E2kResult *result);

void exchange_hierarchy_webdav_construct   (ExchangeHierarchyWebDAV *hwd,
					    ExchangeAccount *account,
					    ExchangeHierarchyType type,
					    const gchar *hierarchy_name,
					    const gchar *physical_uri_prefix,
					    const gchar *internal_uri_prefix,
					    const gchar *owner_name,
					    const gchar *owner_email,
					    const gchar *source_uri,
					    gboolean deep_searchable);

typedef void (*ExchangeHierarchyWebDAVScanCallback)    (ExchangeHierarchy *hier,
							EFolder *folder,
							gpointer user_data);
void    exchange_hierarchy_webdav_offline_scan_subtree (ExchangeHierarchy *hier,
							ExchangeHierarchyWebDAVScanCallback cb,
							gpointer user_data);

gdouble exchange_hierarchy_webdav_get_total_folder_size (ExchangeHierarchyWebDAV *hwd);

G_END_DECLS

#endif /* __EXCHANGE_HIERARCHY_WEBDAV_H__ */
